"""Generate the Wind Fields sample: rolling grassland, wildflowers, shrubs and trees that sway in the scene's wind.

Python standard library only, and deterministic: re-running rewrites the same files byte for byte. Every asset is
procedural (meshes, textures, materials, terrain and the scene), so the sample needs no downloads. It writes only
below this directory.

    python samples/WindFields/generate.py

The scene shows the foliage wind feature (docs/FOLIAGE_WIND.md): a WindComponent drives every foliage type whose
wind is enabled, and each type answers with its own stiffness, flutter and bend height.
"""
from __future__ import annotations

import json
import math
import random
import struct
import uuid
import zlib
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent
ASSETS = ROOT / "Assets"
LIT = "engine://builtin/shadergraph/default-lit"

# Terrain: 256 m square, one sample per metre, centred on the origin.
SAMPLES = 257
CELL = 1.0
CORNER = (-128.0, 0.0, -128.0)
HEIGHT_SCALE = 48.0

# Wind (WindComponent): a fresh breeze from the south-west, with gusts rolling across the field.
WIND = {"Direction": 35.0, "Speed": 7.5, "Gustiness": 0.55, "Gust Size": 28.0}


# ---- files ------------------------------------------------------------------------------------------------------

def write(relative: str, content: str | bytes, meta_records: tuple[str, ...] = ()) -> Path:
    """Writes a file below the sample, with a stable .plutometa identity for assets (plus any extra records)."""
    path = ROOT / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    data = content.encode("utf-8") if isinstance(content, str) else content
    path.write_bytes(data)
    if path.is_relative_to(ASSETS):
        meta = path.with_name(path.name + ".plutometa")
        if meta.exists():
            # The editor adds import records (packages, object identities) to sidecars: keep them and set only the
            # records this script owns. A changed MODEL_OPTIONS makes the editor reimport the model.
            lines = meta.read_text(encoding="utf-8").splitlines()
            for record in meta_records:
                key = record.split("\t", 1)[0]
                index = next((i for i, line in enumerate(lines) if line.split("\t", 1)[0] == key), None)
                if index is None:
                    lines.append(record)
                elif key != "MODEL_IMPORT":  # never downgrade the editor's settings codec version
                    lines[index] = record
        else:
            identity = uuid.uuid5(uuid.NAMESPACE_URL, "plutoge:wind-fields:" + relative).hex
            lines = ["PLUTOASSET\t1", f"ID\t{identity}", "IMPORTER_VERSION\t1", *meta_records]
        meta.write_bytes(("\n".join(lines) + "\n").encode("utf-8"))
    return path


# Model import settings (MeshImportOptions flags): generated LODs, vertex-cache and overdraw optimization, and
# vegetation LODs, whose coarser levels keep fewer, larger blades. Ground cover is most of the scene's triangles, and
# without a LOD chain every distant clump draws at full detail.
VEGETATION_IMPORT = ("MODEL_IMPORT\t1", "MODEL_OPTIONS\t15")


def png(relative: str, width: int, height: int, pixel) -> None:
    """An RGBA8 PNG; pixel(x, y) returns 0..1 floats (r, g, b, a), with y = 0 the top row."""
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            raw.extend(max(0, min(255, round(c * 255.0))) for c in pixel(x, y))

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    write(relative, b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) +
          chunk(b"IEND", b""))


# ---- noise ------------------------------------------------------------------------------------------------------

def hash01(x: int, z: int, seed: int) -> float:
    h = (x * 0x8DA6B343 ^ z * 0xD8163841 ^ seed * 0x27D4EB2D) & 0xFFFFFFFF
    h ^= h >> 15
    h = (h * 0x2C1B3C6D) & 0xFFFFFFFF
    h ^= h >> 12
    h = (h * 0x297A2D39) & 0xFFFFFFFF
    h ^= h >> 15
    return (h & 0xFFFFFF) / float(0x1000000)


def value_noise(x: float, z: float, seed: int) -> float:
    """Smooth value noise in [0, 1]."""
    ix, iz = math.floor(x), math.floor(z)
    fx, fz = x - ix, z - iz
    sx, sz = fx * fx * (3 - 2 * fx), fz * fz * (3 - 2 * fz)
    a, b = hash01(ix, iz, seed), hash01(ix + 1, iz, seed)
    c, d = hash01(ix, iz + 1, seed), hash01(ix + 1, iz + 1, seed)
    return (a + (b - a) * sx) * (1 - sz) + (c + (d - c) * sx) * sz


def fbm(x: float, z: float, seed: int, octaves: int = 4) -> float:
    total, amplitude, scale, norm = 0.0, 1.0, 1.0, 0.0
    for octave in range(octaves):
        total += value_noise(x * scale, z * scale, seed + octave * 101) * amplitude
        norm += amplitude
        amplitude *= 0.5
        scale *= 2.03
    return total / norm


# ---- landscape --------------------------------------------------------------------------------------------------

def trail_distance(x: float, z: float) -> float:
    """Distance (m) from a meandering trail that crosses the meadow from the camera toward the hills."""
    centre = 6.0 * math.sin(z * 0.045 + 0.6) + 3.0 * math.sin(z * 0.11)
    return abs(x - centre)


def raw_height(x: float, z: float) -> float:
    rolling = 2.2 * math.sin(x * 0.021 + 1.3) * math.cos(z * 0.017 + 0.4) + 1.6 * math.sin((x + z) * 0.012)
    bumps = (fbm(x * 0.035, z * 0.035, 7) - 0.5) * 5.0
    # Hills rise behind the meadow (toward -Z) and to its sides, framing the view.
    ridge = 14.0 * max(0.0, (-z - 40.0) / 88.0) ** 1.6 + 6.0 * max(0.0, (abs(x) - 70.0) / 58.0) ** 2
    trail = -0.25 * max(0.0, 1.0 - trail_distance(x, z) / 2.5)
    return 10.0 + rolling + bumps + ridge + trail


HEIGHTS = [[min(HEIGHT_SCALE, max(0.0, raw_height(CORNER[0] + i * CELL, CORNER[2] + j * CELL)))
            for i in range(SAMPLES)] for j in range(SAMPLES)]


def height_at(x: float, z: float) -> float:
    """The terrain's height, interpolated bilinearly as TerrainComponent::SampleHeight does."""
    gx = min(max((x - CORNER[0]) / CELL, 0.0), SAMPLES - 1.0)
    gz = min(max((z - CORNER[2]) / CELL, 0.0), SAMPLES - 1.0)
    i0, j0 = min(int(gx), SAMPLES - 2), min(int(gz), SAMPLES - 2)
    tx, tz = gx - i0, gz - j0
    a, b = HEIGHTS[j0][i0], HEIGHTS[j0][i0 + 1]
    c, d = HEIGHTS[j0 + 1][i0], HEIGHTS[j0 + 1][i0 + 1]
    return CORNER[1] + (a + (b - a) * tx) * (1 - tz) + (c + (d - c) * tx) * tz


def meadow_colour(x: float, z: float) -> tuple[float, float, float]:
    """The ground's painted colour: meadow greens, sun-dried patches and the bare trail."""
    lush = fbm(x * 0.05, z * 0.05, 3)
    dry = max(0.0, fbm(x * 0.02 + 40, z * 0.02, 11) - 0.55) * 2.5
    base = (0.16 + 0.05 * lush, 0.27 + 0.08 * lush, 0.07)
    straw = (0.42, 0.38, 0.16)
    colour = tuple(b + (s - b) * min(dry, 0.8) for b, s in zip(base, straw))
    trail = max(0.0, 1.0 - trail_distance(x, z) / 1.6)
    soil = (0.30, 0.24, 0.16)
    return tuple(c + (s - c) * trail for c, s in zip(colour, soil))


# ---- meshes -----------------------------------------------------------------------------------------------------

@dataclass
class Mesh:
    positions: list = field(default_factory=list)
    normals: list = field(default_factory=list)
    uvs: list = field(default_factory=list)
    indices: list = field(default_factory=list)

    def vertex(self, position, normal, uv) -> int:
        self.positions.append(tuple(position))
        self.normals.append(normalize(normal))
        self.uvs.append(tuple(uv))
        return len(self.positions) - 1

    def quad(self, a, b, c, d) -> None:
        self.indices += [a, b, c, a, c, d]

    def height(self) -> float:
        return max(p[1] for p in self.positions)


def normalize(v):
    length = math.sqrt(sum(c * c for c in v)) or 1.0
    return tuple(c / length for c in v)


def glb(relative: str, mesh: Mesh, meta_records: tuple[str, ...] = ()) -> None:
    """One primitive (positions, normals, UVs, uint32 triangles) as a binary glTF."""
    positions = b"".join(struct.pack("<3f", *p) for p in mesh.positions)
    normals = b"".join(struct.pack("<3f", *n) for n in mesh.normals)
    uvs = b"".join(struct.pack("<2f", *t) for t in mesh.uvs)
    indices = b"".join(struct.pack("<I", i) for i in mesh.indices)
    binary = positions + normals + uvs + indices
    offsets = [0, len(positions), len(positions) + len(normals), len(positions) + len(normals) + len(uvs)]
    name = Path(relative).stem
    document = {
        "asset": {"version": "2.0", "generator": "PlutoGE WindFields generate.py"},
        "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": name}],
        "meshes": [{"name": name, "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                                                  "indices": 3}]}],
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": offsets[0], "byteLength": len(positions), "target": 34962},
            {"buffer": 0, "byteOffset": offsets[1], "byteLength": len(normals), "target": 34962},
            {"buffer": 0, "byteOffset": offsets[2], "byteLength": len(uvs), "target": 34962},
            {"buffer": 0, "byteOffset": offsets[3], "byteLength": len(indices), "target": 34963}],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": len(mesh.positions), "type": "VEC3",
             "min": [min(p[i] for p in mesh.positions) for i in range(3)],
             "max": [max(p[i] for p in mesh.positions) for i in range(3)]},
            {"bufferView": 1, "componentType": 5126, "count": len(mesh.normals), "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": len(mesh.uvs), "type": "VEC2"},
            {"bufferView": 3, "componentType": 5125, "count": len(mesh.indices), "type": "SCALAR"}],
    }
    metadata = json.dumps(document, separators=(",", ":")).encode()
    metadata += b" " * (-len(metadata) % 4)
    data = struct.pack("<III", 0x46546C67, 2, 28 + len(metadata) + len(binary))
    data += struct.pack("<II", len(metadata), 0x4E4F534A) + metadata
    data += struct.pack("<II", len(binary), 0x004E4942) + binary
    write(relative, data, meta_records)


def blade(mesh: Mesh, root, yaw: float, height: float, width: float, lean: float, segments: int,
          u0: float = 0.0, u1: float = 1.0) -> None:
    """A tapered, curving blade from root: `lean` is how far (m) its tip arcs away along its facing."""
    facing = (math.sin(yaw), 0.0, math.cos(yaw))
    side = (math.cos(yaw), 0.0, -math.sin(yaw))
    # Normals point straight up, so blades shade like the ground they grow from. Grass materials are one-sided:
    # instanced foliage draws without back-face culling, and a two-sided material would shade a blade's far side with
    # its normal reversed (dark) wherever the blade is above the camera, as on rising ground. Back faces must not be
    # added as extra triangles either: coincident copies pass the depth-equal test together and double the shading.
    normal = (0.0, 1.0, 0.0)
    rows = []
    for s in range(segments + 1):
        t = s / segments
        bend = lean * t * t
        centre = (root[0] + facing[0] * bend, root[1] + height * t * (1 - 0.15 * t * t), root[2] + facing[2] * bend)
        half = width * 0.5 * (1.0 - t) ** 0.8
        if s == segments:
            rows.append([mesh.vertex(centre, normal, ((u0 + u1) * 0.5, t))])
        else:
            rows.append([mesh.vertex(tuple(c - sd * half for c, sd in zip(centre, side)), normal, (u0, t)),
                         mesh.vertex(tuple(c + sd * half for c, sd in zip(centre, side)), normal, (u1, t))])
    for s in range(segments):
        a, b = rows[s]
        if len(rows[s + 1]) == 1:
            mesh.indices += [a, b, rows[s + 1][0]]
        else:
            c, d = rows[s + 1]
            mesh.quad(a, b, d, c)


def grass_clump(seed: int, blades: int, height: tuple, width: float, spread: float) -> Mesh:
    rng = random.Random(seed)
    mesh = Mesh()
    for _ in range(blades):
        angle, radius = rng.uniform(0, math.tau), spread * math.sqrt(rng.random())
        root = (math.cos(angle) * radius, 0.0, math.sin(angle) * radius)
        h = rng.uniform(*height)
        blade(mesh, root, rng.uniform(0, math.tau), h, width * rng.uniform(0.8, 1.2), h * rng.uniform(0.1, 0.35), 3)
    return mesh


# Flower atlas cells (u ranges): stem green, then petal colours, then the flower centre.
ATLAS = {"stem": (0.0, 0.2), "yellow": (0.2, 0.4), "white": (0.4, 0.6), "violet": (0.6, 0.8), "centre": (0.8, 1.0)}


def flower_clump(seed: int, petal: str) -> Mesh:
    rng = random.Random(seed)
    mesh = Mesh()
    for _ in range(rng.randint(4, 6)):
        angle, radius = rng.uniform(0, math.tau), 0.12 * math.sqrt(rng.random())
        root = (math.cos(angle) * radius, 0.0, math.sin(angle) * radius)
        h = rng.uniform(0.3, 0.55)
        yaw = rng.uniform(0, math.tau)
        lean = h * rng.uniform(0.05, 0.2)
        blade(mesh, root, yaw, h, 0.012, lean, 3, *ATLAS["stem"])
        tip = (root[0] + math.sin(yaw) * lean, h * (1 - 0.15), root[2] + math.cos(yaw) * lean)
        # A star of petals facing up, and a raised centre.
        petals, size = 6, rng.uniform(0.04, 0.055)
        u0, u1 = ATLAS[petal]
        centre = mesh.vertex(tip, (0, 1, 0), ((u0 + u1) / 2, 0.5))
        ring = []
        for k in range(petals * 2):
            a = math.tau * k / (petals * 2)
            r = size if k % 2 == 0 else size * 0.45
            ring.append(mesh.vertex((tip[0] + math.cos(a) * r, tip[1] + 0.004, tip[2] + math.sin(a) * r), (0, 1, 0),
                                    (u0 + (u1 - u0) * (0.5 + 0.5 * math.cos(a)), 0.5 + 0.5 * math.sin(a))))
        for k in range(len(ring)):
            mesh.indices += [centre, ring[(k + 1) % len(ring)], ring[k]]
        cu0, cu1 = ATLAS["centre"]
        dot = mesh.vertex((tip[0], tip[1] + 0.012, tip[2]), (0, 1, 0), ((cu0 + cu1) / 2, 0.5))
        for k in range(0, len(ring), 2):
            mesh.indices += [dot, ring[(k + 2) % len(ring)], ring[k]]
    return mesh


def tube(mesh: Mesh, start, end, r0: float, r1: float, sides: int = 8) -> None:
    """A tapered, capped cylinder from start to end (bark)."""
    axis = tuple(e - s for s, e in zip(start, end))
    length = math.sqrt(sum(a * a for a in axis))
    w = normalize(axis)
    helper = (1.0, 0.0, 0.0) if abs(w[1]) > 0.9 else (0.0, 1.0, 0.0)
    u = normalize((helper[1] * w[2] - helper[2] * w[1], helper[2] * w[0] - helper[0] * w[2], helper[0] * w[1] - helper[1] * w[0]))
    v = (w[1] * u[2] - w[2] * u[1], w[2] * u[0] - w[0] * u[2], w[0] * u[1] - w[1] * u[0])
    rings = []
    for end_index, (point, radius) in enumerate(((start, r0), (end, r1))):
        ring = []
        for k in range(sides + 1):
            a = math.tau * k / sides
            n = tuple(math.cos(a) * u[i] + math.sin(a) * v[i] for i in range(3))
            ring.append(mesh.vertex(tuple(point[i] + n[i] * radius for i in range(3)), n,
                                    (k / sides, end_index * length / 1.5)))
        rings.append(ring)
    for k in range(sides):
        mesh.quad(rings[0][k], rings[0][k + 1], rings[1][k + 1], rings[1][k])
    cap = mesh.vertex(end, w, (0.5, 0.5))
    for k in range(sides):
        mesh.indices += [rings[1][k], rings[1][k + 1], cap]


@dataclass
class Tree:
    trunk: Mesh
    canopy: Mesh
    height: float


def tree(seed: int, height: float, crown: float) -> Tree:
    """A broadleaf tree: a trunk with a few limbs, and a crown of leaf cards around the limb ends."""
    rng = random.Random(seed)
    trunk, canopy = Mesh(), Mesh()
    fork = height * 0.45
    tube(trunk, (0, -0.3, 0), (0, fork, 0), 0.32, 0.22, 10)
    centres = []
    for limb in range(5):
        a = math.tau * limb / 5 + rng.uniform(-0.3, 0.3)
        reach = crown * rng.uniform(0.5, 0.8)
        tip = (math.cos(a) * reach, height * rng.uniform(0.7, 0.88), math.sin(a) * reach)
        tube(trunk, (0, fork - 0.3, 0), tip, 0.16, 0.06, 6)
        centres.append(tip)
    tube(trunk, (0, fork - 0.2, 0), (rng.uniform(-0.3, 0.3), height * 0.93, rng.uniform(-0.3, 0.3)), 0.15, 0.05, 6)
    centres.append((0, height * 0.88, 0))
    crown_centre = (0, height * 0.72, 0)
    for centre in centres:
        for _ in range(34):
            # Cards scattered in a squashed ball around each limb end, facing every way.
            d = normalize((rng.gauss(0, 1), rng.gauss(0, 0.7), rng.gauss(0, 1)))
            r = crown * 0.42 * rng.random() ** 0.5
            p = tuple(c + d[i] * r for i, c in enumerate(centre))
            leaf_card(canopy, p, rng, crown_centre, rng.uniform(0.9, 1.3))
    return Tree(trunk, canopy, height)


def leaf_card(mesh: Mesh, centre, rng: random.Random, light_centre, size: float) -> None:
    """A square card of leaves; its normals point away from the crown's centre, so the crown shades like a ball."""
    yaw, pitch = rng.uniform(0, math.tau), rng.uniform(-0.9, 0.9)
    right = (math.cos(yaw), 0.0, -math.sin(yaw))
    up = (math.sin(yaw) * math.sin(pitch), math.cos(pitch), math.cos(yaw) * math.sin(pitch))
    h = size * 0.5
    corners = [(-h, -h, 0, 0), (h, -h, 1, 0), (h, h, 1, 1), (-h, h, 0, 1)]
    ids = []
    for cx, cy, u, v in corners:
        p = tuple(centre[i] + right[i] * cx + up[i] * cy for i in range(3))
        n = normalize(tuple(p[i] - light_centre[i] + (0, 0.6, 0)[i] for i in range(3)))
        ids.append(mesh.vertex(p, n, (u, v)))
    mesh.quad(*ids)


def shrub(seed: int) -> Mesh:
    rng = random.Random(seed)
    mesh = Mesh()
    for _ in range(34):
        d = normalize((rng.gauss(0, 1), abs(rng.gauss(0, 0.8)) + 0.1, rng.gauss(0, 1)))
        r = 0.85 * rng.random() ** 0.4
        p = (d[0] * r * 1.2, 0.35 + d[1] * r * 0.8, d[2] * r * 1.2)
        leaf_card(mesh, p, rng, (0, 0.3, 0), rng.uniform(0.55, 0.8))
    return mesh


def rock(seed: int) -> Mesh:
    """A lumpy boulder, half buried: a displaced sphere."""
    rng = random.Random(seed)
    mesh = Mesh()
    rings, sides = 8, 12
    grid = []
    for r in range(rings + 1):
        phi = math.pi * r / rings
        row = []
        for s in range(sides + 1):
            theta = math.tau * (s % sides) / sides
            n = (math.sin(phi) * math.cos(theta), math.cos(phi), math.sin(phi) * math.sin(theta))
            bump = 1.0 + 0.18 * (value_noise(n[0] * 2.5 + 7, n[2] * 2.5 + n[1] * 1.7, seed) - 0.5)
            p = (n[0] * 1.1 * bump, n[1] * 0.7 * bump - 0.15, n[2] * 0.95 * bump)
            row.append(mesh.vertex(p, n, (s / sides, r / rings)))
        grid.append(row)
    for r in range(rings):
        for s in range(sides):
            mesh.quad(grid[r][s], grid[r][s + 1], grid[r + 1][s + 1], grid[r + 1][s])
    return mesh


# ---- textures ---------------------------------------------------------------------------------------------------

def textures() -> None:
    def grass_pixel(x, y):
        t = 1.0 - y / 63.0  # v = 0 at the root (the bottom row)
        root, mid, tip = (0.09, 0.17, 0.04), (0.30, 0.48, 0.11), (0.60, 0.66, 0.28)
        a, b, s = (root, mid, t / 0.6) if t < 0.6 else (mid, tip, (t - 0.6) / 0.4)
        edge = 0.85 + 0.15 * math.sin(math.pi * x / 15.0)
        return (*(edge * (p + (q - p) * s) for p, q in zip(a, b)), 1.0)
    png("Assets/Textures/GrassBlade.png", 16, 64, grass_pixel)

    colours = {"stem": (0.18, 0.36, 0.08), "yellow": (0.95, 0.78, 0.12), "white": (0.93, 0.93, 0.88),
               "violet": (0.52, 0.30, 0.78), "centre": (0.95, 0.62, 0.08)}
    cells = list(ATLAS.items())

    def atlas_pixel(x, y):
        for name, (u0, u1) in cells:
            if u0 <= (x + 0.5) / 80.0 < u1:
                return (*colours[name], 1.0)
        return (1, 1, 1, 1)
    png("Assets/Textures/FlowerAtlas.png", 80, 16, atlas_pixel)

    def bark_pixel(x, y):
        streak = value_noise(x * 0.35, y * 0.04, 21) * 0.6 + value_noise(x * 1.3, y * 0.2, 22) * 0.4
        return (0.20 + 0.12 * streak, 0.15 + 0.08 * streak, 0.10 + 0.05 * streak, 1.0)
    png("Assets/Textures/Bark.png", 64, 64, bark_pixel)

    # Leaves: ellipses scattered over a disc, alpha-masked so cards read as clusters of leaves, not squares.
    rng = random.Random(5)
    leaves = []
    for _ in range(46):
        a, r = rng.uniform(0, math.tau), 0.42 * math.sqrt(rng.random())
        leaves.append((0.5 + math.cos(a) * r, 0.5 + math.sin(a) * r, rng.uniform(0, math.pi),
                       rng.uniform(0.05, 0.08), rng.uniform(0.6, 1.0)))

    def leaf_pixel(x, y):
        u, v = (x + 0.5) / 128.0, (y + 0.5) / 128.0
        for cx, cy, angle, length, shade in reversed(leaves):
            dx, dy = u - cx, v - cy
            along = dx * math.cos(angle) + dy * math.sin(angle)
            across = -dx * math.sin(angle) + dy * math.cos(angle)
            if (along / length) ** 2 + (across / (length * 0.45)) ** 2 <= 1.0:
                vein = 0.85 + 0.15 * min(1.0, abs(across) / (length * 0.12))
                return (0.16 * shade * vein, 0.34 * shade * vein, 0.07 * shade * vein, 1.0)
        return (0.16, 0.30, 0.07, 0.0)
    png("Assets/Textures/Leaves.png", 128, 128, leaf_pixel)

    def meadow_pixel(x, y):
        # Row 0 is the terrain's far (-Z) edge in the painted map's convention (the terrain flips V).
        wx = CORNER[0] + (x + 0.5) / 1024.0 * (SAMPLES - 1) * CELL
        wz = CORNER[2] + (y + 0.5) / 1024.0 * (SAMPLES - 1) * CELL
        return (*meadow_colour(wx, wz), 1.0)
    png("Assets/Textures/MeadowAlbedo.png", 1024, 1024, meadow_pixel)


# ---- materials --------------------------------------------------------------------------------------------------

def material(name: str, colour, roughness: float, albedo: str = "", alpha: str = "Opaque", two_sided: bool = False,
             subsurface: float = 0.0, subsurface_colour=(0.5, 0.65, 0.2), shadows: bool = True) -> str:
    lines = [f"Color={colour[0]},{colour[1]},{colour[2]},1", "SurfaceType=Standard", f"AlphaMode={alpha}",
             "AlphaCutoff=0.5", f"CastsShadow={'true' if shadows else 'false'}",
             f"TwoSided={'true' if two_sided else 'false'}", "Metallic=0", f"Roughness={roughness}",
             f"Subsurface={subsurface}", "SubsurfaceColor={},{},{}".format(*subsurface_colour),
             f"AlbedoTexture={albedo}", f"ShaderGraph={LIT}"]
    write(f"Assets/Materials/{name}.plutomaterial", "\n".join(lines) + "\n")
    return f"project://Materials/{name}.plutomaterial"


# ---- scattering -------------------------------------------------------------------------------------------------

@dataclass
class Placement:
    x: float
    z: float
    yaw: float
    scale: float


def scatter(seed: int, spacing: float, bounds, density, scale=(0.8, 1.2)) -> list[Placement]:
    """A jittered grid: one candidate per spacing-sized cell, kept with probability density(x, z)."""
    rng = random.Random(seed)
    (x0, z0), (x1, z1) = bounds
    out = []
    z = z0
    while z < z1:
        x = x0
        while x < x1:
            px, pz = x + rng.random() * spacing, z + rng.random() * spacing
            if rng.random() < density(px, pz):
                out.append(Placement(px, pz, rng.uniform(0, 360), rng.uniform(*scale)))
            x += spacing
        z += spacing
    return out


def spatial_key(p: Placement, cell: float):
    """Cell, then a Morton curve inside it: instances of a 64-instance chunk stay close (docs/FOLIAGE.md)."""
    cx, cz = math.floor(p.x / cell), math.floor(p.z / cell)
    u = int((p.x / cell - cx) * 65535.0) & 0xFFFF
    v = int((p.z / cell - cz) * 65535.0) & 0xFFFF
    morton = 0
    for bit in range(16):
        morton |= ((u >> bit) & 1) << (2 * bit) | ((v >> bit) & 1) << (2 * bit + 1)
    return cz, cx, morton


# ---- scene ------------------------------------------------------------------------------------------------------

def f(value: float) -> str:
    return f"{value:.6f}"


def vec(values) -> str:
    return ",".join(f(v) for v in values)


class Scene:
    def __init__(self):
        self.lines = ["SCENE\t1"]
        self.next_id = 1
        self.next_instance = 1

    def entity(self, name: str, position=(0, 0, 0), rotation=(0, 0, 0), scale=(1, 1, 1)) -> int:
        entity_id = self.next_id
        self.next_id += 1
        self.lines.append(f"ENTITY\t{entity_id}\t0\t1\t{name}\t{vec(position)}\t{vec(rotation)}\t{vec(scale)}")
        return entity_id

    def component(self, entity_id: int, kind: str, properties) -> None:
        self.lines.append(f"COMPONENT\t{entity_id}\t{kind}\t1")
        for prop in properties:
            name, type_id, value = prop[:3]
            self.lines.append(f"PROPERTY\t{name}\t{type_id}\t{value}" + ("" if type_id == 6 else "\t0"))
        self.lines.append("END_COMPONENT")


@dataclass
class FoliageType:
    name: str
    mesh: str
    material: str
    placements: list
    wind: dict | None  # stiffness, flutter, bend (and optional pivot) heights; None stays still
    cell: float = 32.0
    draw_distance: float = 0.0
    collision: tuple | None = None  # (centre y, radius, height)
    stretch: tuple = (1.0, 1.0, 1.0)  # per-axis factors on each placement's scale


def heading_euler(yaw: float, pitch: float):
    """Euler angles for turning by yaw about +Y (0 looks toward -Z), then tilting by pitch (up) about the turned X."""
    sy, cy = math.sin(math.radians(yaw)), math.cos(math.radians(yaw))
    sp, cp = math.sin(math.radians(pitch)), math.cos(math.radians(pitch))
    return (math.degrees(math.atan2(sp, cy * cp)), math.degrees(math.asin(max(-1.0, min(1.0, sy * cp)))),
            math.degrees(math.atan2(-sy * sp, cy)))


def look(eye, target):
    d = [t - e for e, t in zip(eye, target)]
    yaw = math.degrees(math.atan2(-d[0], -d[2]))
    pitch = math.degrees(math.atan2(d[1], math.hypot(d[0], d[2])))
    return heading_euler(yaw, pitch)


def foliage(scene: Scene, name: str, types: list[FoliageType], draw_distance: float, shadow_distance: float,
            shadows: bool) -> None:
    entity_id = scene.entity(name)
    props = [("PaintEnabled", 4, "false"), ("Density", 1, "1"), ("MinScale", 0, f(1)), ("MaxScale", 0, f(1)),
             ("MaxDrawDistance", 0, f(draw_distance)), ("MaxShadowDistance", 0, f(shadow_distance)),
             ("MinRenderLod", 1, "0"), ("MinShadowLod", 1, "0"), ("CastShadows", 4, "true" if shadows else "false"),
             ("Static", 4, "true"), ("SelectedType", 1, "0"), ("FoliageTypeCount", 1, str(len(types)))]
    for index, t in enumerate(types):
        records = []
        for p in sorted(t.placements, key=lambda p: spatial_key(p, t.cell)):
            records.append(",".join([str(scene.next_instance)] +
                                    [f"{v:.3f}" for v in (p.x, height_at(p.x, p.z), p.z, 0.0, p.yaw, 0.0)] +
                                    [f"{p.scale * s:.3f}" for s in t.stretch]))
            scene.next_instance += 1
        prefix = f"Type.{index}."
        props += [(prefix + "Name", 2, t.name), (prefix + "SourceMesh", 2, t.mesh),
                  (prefix + "MaterialAsset", 2, t.material), (prefix + "SubmeshIndex", 1, "-1"),
                  (prefix + "UseGeneratedLods", 4, "false"), (prefix + "AlignToTerrainNormal", 4, "false"),
                  (prefix + "CellSize", 0, f(t.cell)), (prefix + "MaxDrawDistance", 0, f(t.draw_distance))]
        if t.collision:
            centre, radius, height = t.collision
            props += [(prefix + "CollisionEnabled", 4, "true"), (prefix + "CollisionCenter", 3, vec((0, centre, 0))),
                      (prefix + "CollisionRadius", 0, f(radius)), (prefix + "CollisionHeight", 0, f(height))]
        else:
            props.append((prefix + "CollisionEnabled", 4, "false"))
        if t.wind:
            props += [(prefix + "WindEnabled", 4, "true"), (prefix + "WindStiffness", 0, f(t.wind["stiffness"])),
                      (prefix + "WindFlutter", 0, f(t.wind["flutter"])),
                      (prefix + "WindBendHeight", 0, f(t.wind["bend"])),
                      (prefix + "WindPivotHeight", 0, f(t.wind.get("pivot", 0.0)))]
        else:
            props.append((prefix + "WindEnabled", 4, "false"))
        props.append((prefix + "Instances", 2, ";".join(records)))
    scene.component(entity_id, "FoliageComponent", props)


def camera(scene: Scene, name: str, eye, target, main: bool, fov: float = 55.0) -> None:
    entity_id = scene.entity(name, eye, look(eye, target))
    scene.component(entity_id, "CameraComponent", [
        ("FOV", 0, f(fov)), ("Projection", 6, "0\t2\tPerspective\tOrthographic"),
        ("OrthographicHeight", 0, f(10)), ("NearPlane", 0, f(0.1)), ("FarPlane", 0, f(1200)),
        ("MainCamera", 4, "true" if main else "false"), ("RenderType", 6, "0\t2\tBase\tOverlay"),
        ("OverlayOrder", 1, "0"), ("RenderTags", 2, ""), ("IgnoredTags", 2, ""), ("TargetTexture", 2, ""),
        ("TransparentBackground", 4, "false"), ("FilterLightsByTags", 4, "false"), ("RenderRuntimeUI", 4, "false"),
        ("PostProcessPresetAsset", 2, "project://PostProcessing/Meadow.plutopostprocess")])


# Review viewpoints (capture.py renders each by its camera entity's name).
VIEWS = {
    "CAMERA - Meadow": ((-22.0, 0.0, 58.0), 1.6, (8.0, 0.0, -30.0), 4.0),
    "CAMERA - Grass Close": ((-6.0, 0.0, 20.0), 0.9, (-1.0, 0.0, 12.0), 0.5),
    "CAMERA - Tree Line": ((30.0, 0.0, 10.0), 2.2, (40.0, 0.0, -48.0), 6.0),
}


def build_scene() -> str:
    scene = Scene()

    # Afternoon sun from the west, low enough for long shadows and grass lit from behind.
    sun_direction = normalize((0.5, -0.72, -0.48))
    # The light shines along its entity's forward axis, so the entity is turned to face along the direction too.
    sun = scene.entity("Sun", (0, 40, 0), look((0, 0, 0), sun_direction))
    scene.component(sun, "LightComponent", [
        ("Color", 3, vec((1.0, 0.95, 0.86))), ("Intensity", 0, f(14)), ("CastsShadows", 4, "true"),
        ("Direction", 3, vec(sun_direction)), ("LightType", 6, "1\t4\tPoint\tDirectional\tSpot\tRect"),
        ("Static", 4, "true"),
        ("Shadow Method", 6, "1\t2\tCascaded Shadow Maps (Legacy)\tVirtual Shadow Maps (Default)"),
        ("Shadow Distance (0 = Camera Far)", 0, f(140)), ("Shadow Softness (0 = Hard Shadows)", 0, f(1.5)),
        ("Contact Shadow Length (0 = Off)", 0, f(0.25))])

    # capture.py renders each view by its camera's name; the first is the main camera.
    for index, (name, (ground, eye_height, target, target_height)) in enumerate(VIEWS.items()):
        eye = (ground[0], height_at(ground[0], ground[2]) + eye_height, ground[2])
        aim = (target[0], height_at(target[0], target[2]) + target_height, target[2])
        camera(scene, name, eye, aim, index == 0)

    sky = scene.entity("Sky", (0, 900, 0))
    scene.component(sky, "PhysicalSkyComponent", [
        ("Rayleigh Strength", 0, f(1)), ("Mie Strength", 0, f(1.2)), ("Mie Anisotropy", 0, f(0.8)),
        ("Ozone Strength", 0, f(1)), ("Sun Intensity", 0, f(12)), ("Sun Angular Radius", 0, f(0.27)),
        ("Sun Color", 3, vec((1.0, 0.97, 0.9))), ("Exposure", 0, f(1))])
    wind_toward = (math.cos(math.radians(WIND["Direction"])), 0.0, math.sin(math.radians(WIND["Direction"])))
    scene.component(sky, "VolumetricCloudComponent", [
        ("Size", 3, vec((16000, 420, 16000))), ("Coverage", 0, f(0.32)), ("Density", 0, f(0.45)),
        # The clouds drift with the scene's wind.
        ("Wind Direction", 3, vec(wind_toward)), ("Wind Speed", 0, f(WIND["Speed"] * 1.5)),
        ("Cast Shadows", 4, "true")])

    wind = scene.entity("Wind")
    scene.component(wind, "WindComponent", [(name, 0, f(value)) for name, value in WIND.items()])

    terrain = scene.entity("Meadow", CORNER)
    samples = ",".join(f"{h:.3f}".rstrip("0").rstrip(".") for row in HEIGHTS for h in row)
    scene.component(terrain, "TerrainComponent", [
        ("Width", 1, str(SAMPLES)), ("Depth", 1, str(SAMPLES)), ("CellSize", 0, f(CELL)),
        ("HeightScale", 0, f(HEIGHT_SCALE)), ("SurfaceSmoothing", 0, f(0.35)), ("ChunkSize", 1, "32"),
        ("LodCount", 1, "4"), ("Static", 4, "true"), ("HeightMap", 2, ""), ("HeightSamples", 2, samples),
        ("MaterialAsset", 2, "project://Materials/Ground.plutomaterial"),
        ("PaintedAlbedo", 2, "project://Textures/MeadowAlbedo.png"), ("PaintEnabled", 4, "false")])
    scene.component(terrain, "ColliderComponent", [
        ("Shape", 6, "3\t5\tBox\tSphere\tCapsule\tTerrain\tMesh"), ("Center", 3, vec((0, 0, 0))),
        ("Size", 3, vec((1, 1, 1))), ("Radius", 0, f(0.5)), ("Height", 0, f(2)), ("Is Trigger", 4, "false"),
        ("Surface Asset", 2, ""), ("Blocks Audio", 4, "true")])

    # ---- vegetation ----
    bounds = ((-96.0, -80.0), (96.0, 96.0))
    meadow = lambda lo, hi: lambda x, z: (lo + (hi - lo) * fbm(x * 0.04, z * 0.04, 31)) * min(1.0, trail_distance(x, z) / 1.8)
    short = scatter(101, 0.62, bounds, meadow(0.55, 1.0), (0.75, 1.25))
    tall = scatter(102, 1.4, bounds, lambda x, z: max(0.0, fbm(x * 0.03, z * 0.03, 41) - 0.45) * 2.2 *
                   min(1.0, trail_distance(x, z) / 3.0), (0.8, 1.2))
    dry = scatter(103, 1.0, bounds, lambda x, z: max(0.0, fbm(x * 0.02 + 40, z * 0.02, 11) - 0.55) * 3.0, (0.8, 1.2))
    flowers = {colour: scatter(110 + i, 1.6, bounds, lambda x, z, i=i: max(0.0, fbm(x * 0.05 + i * 9, z * 0.05, 51 + i) - 0.52) * 2.6
                               * min(1.0, trail_distance(x, z) / 2.0), (0.8, 1.2))
               for i, colour in enumerate(("yellow", "white", "violet"))}

    # Trees: a loose line along the hills, a few standing alone in the meadow.
    rng = random.Random(7)
    trees = []
    for k in range(34):
        x = -110 + k * 6.5 + rng.uniform(-2.5, 2.5)
        z = -58 - 18 * math.sin(k * 0.7) ** 2 + rng.uniform(-6, 4)
        trees.append(Placement(x, z, rng.uniform(0, 360), rng.uniform(0.85, 1.2)))
    for x, z in ((-34, 12), (24, -8), (46, 30), (-60, -24), (8, 34)):
        trees.append(Placement(x, z, rng.uniform(0, 360), rng.uniform(1.0, 1.25)))
    shrubs = [Placement(t.x + rng.uniform(-5, 5), t.z + rng.uniform(3, 8), rng.uniform(0, 360), rng.uniform(0.7, 1.3))
              for t in trees for _ in range(rng.randint(1, 3))]
    shrubs += scatter(120, 9.0, bounds, lambda x, z: 0.12 * min(1.0, trail_distance(x, z) / 4.0), (0.6, 1.1))
    rocks = scatter(130, 14.0, ((-110, -100), (110, 110)), lambda x, z: 0.3 * min(1.0, trail_distance(x, z) / 3.0),
                    (0.4, 1.3))

    grass_wind = {"stiffness": 0.15, "flutter": 0.6, "bend": GRASS_SHORT_HEIGHT}
    foliage(scene, "Vegetation: Grass", [
        FoliageType("Meadow grass", "project://Models/GrassShort.glb", MATERIALS["grass"], short, grass_wind,
                    cell=48.0),
        FoliageType("Dry grass", "project://Models/GrassShort.glb", MATERIALS["dry_grass"], dry, grass_wind, cell=48.0),
        FoliageType("Tall grass", "project://Models/GrassTall.glb", MATERIALS["grass"], tall,
                    {"stiffness": 0.1, "flutter": 0.55, "bend": GRASS_TALL_HEIGHT}, cell=48.0),
    ] + [FoliageType(f"Wildflowers ({colour})", f"project://Models/Flowers{colour.title()}.glb", MATERIALS["flowers"],
                     placements, {"stiffness": 0.3, "flutter": 0.45, "bend": 0.5}, cell=48.0)
         for colour, placements in flowers.items()],
        # Ground cover casts no shadow-map shadows (contact shadows ground it): swaying casters redraw their shadow
        # pages every frame, and a field of them would dominate the shadow budget on integrated GPUs.
        draw_distance=65.0, shadow_distance=0.0, shadows=False)

    tree_wind = {"stiffness": 0.85, "flutter": 0.35, "bend": TREE_HEIGHT, "pivot": TREE_HEIGHT * 0.2}
    foliage(scene, "Vegetation: Trees", [
        # Trunk and crown share placements and the same response, so they bend together.
        FoliageType("Tree trunk", "project://Models/TreeTrunk.glb", MATERIALS["bark"], trees, tree_wind, cell=64.0,
                    collision=(2.0, 0.35, 4.0)),
        FoliageType("Tree crown", "project://Models/TreeCrown.glb", MATERIALS["leaves"], trees, tree_wind, cell=64.0),
    ], draw_distance=600.0, shadow_distance=160.0, shadows=True)
    foliage(scene, "Vegetation: Shrubs", [
        FoliageType("Shrub", "project://Models/Shrub.glb", MATERIALS["shrub"], shrubs,
                    {"stiffness": 0.55, "flutter": 0.5, "bend": 1.2}, cell=64.0),
    ], draw_distance=220.0, shadow_distance=90.0, shadows=True)

    # Rocks have no wind response: they stay still beside the swaying plants.
    foliage(scene, "Vegetation: Rocks", [
        FoliageType("Boulder", "project://Models/Rock.glb", MATERIALS["rock"], rocks, None, cell=64.0,
                    stretch=(1.2, 1.0, 1.0)),
    ], draw_distance=400.0, shadow_distance=120.0, shadows=True)

    counts = {"short grass": len(short), "tall grass": len(tall), "dry grass": len(dry),
              "flowers": sum(len(p) for p in flowers.values()), "trees": len(trees), "shrubs": len(shrubs),
              "rocks": len(rocks)}
    print("Instances: " + ", ".join(f"{count} {name}" for name, count in counts.items()))
    return "\n".join(scene.lines) + "\n"


# ---- assets -----------------------------------------------------------------------------------------------------

GRASS_SHORT = grass_clump(11, 18, (0.32, 0.62), 0.05, 0.26)
GRASS_TALL = grass_clump(12, 11, (0.7, 1.15), 0.04, 0.22)
GRASS_SHORT_HEIGHT = round(GRASS_SHORT.height(), 3)
GRASS_TALL_HEIGHT = round(GRASS_TALL.height(), 3)
TREE = tree(21, 8.5, 3.7)
TREE_HEIGHT = round(max(TREE.trunk.height(), TREE.canopy.height()), 3)
MATERIALS: dict[str, str] = {}


def build_assets() -> None:
    textures()
    MATERIALS.update({
        "grass": material("Grass", (1.0, 1.0, 1.0), 0.75, "project://Textures/GrassBlade.png",
                          subsurface=0.35, subsurface_colour=(0.45, 0.62, 0.15)),
        "dry_grass": material("DryGrass", (1.25, 1.1, 0.7), 0.8, "project://Textures/GrassBlade.png",
                              subsurface=0.3, subsurface_colour=(0.6, 0.55, 0.2)),
        "flowers": material("Wildflowers", (1.0, 1.0, 1.0), 0.6, "project://Textures/FlowerAtlas.png",
                            subsurface=0.25),
        "bark": material("Bark", (1.0, 1.0, 1.0), 0.9, "project://Textures/Bark.png"),
        "leaves": material("Leaves", (1.0, 1.0, 1.0), 0.7, "project://Textures/Leaves.png", alpha="Mask",
                           two_sided=True, subsurface=0.4, subsurface_colour=(0.45, 0.6, 0.12)),
        "shrub": material("ShrubLeaves", (0.8, 0.9, 0.75), 0.75, "project://Textures/Leaves.png", alpha="Mask",
                          two_sided=True, subsurface=0.35, subsurface_colour=(0.4, 0.55, 0.12)),
        "rock": material("Rock", (0.42, 0.41, 0.38), 0.85),
    })
    material("Ground", (1.0, 1.0, 1.0), 0.95)
    build_models()

    # SSAO is present but off. It needs the full G-buffer (normals, material and indirect light), which more than
    # doubles the geometry pass on integrated GPUs (31 -> 13 ms at 1080p Medium on a Vega 8); contact shadows already
    # ground the grass. Enable it in the Post Process editor on faster GPUs.
    write("Assets/PostProcessing/Meadow.plutopostprocess", "\n".join([
        "PostProcessPresetVersion 1 5",
        'Effect "SSAO" 0 8', 'Parameter 0 "Radius" "0.8" 0', 'Parameter 0 "Bias" "0.03" 0',
        'Parameter 0 "Intensity" "0.8" 0', 'Parameter 0 "Power" "1" 0', 'Parameter 1 "Samples" "16" 0',
        'Parameter 1 "Blur Radius" "2" 0', 'Parameter 3 "Half Resolution" "true" 0', 'Parameter 3 "AO Only" "false" 0',
        'Effect "Bloom" 1 4', 'Parameter 0 "Intensity" "0.05" 0', 'Parameter 0 "Threshold" "2" 0',
        'Parameter 0 "Radius" "0.7" 0', 'Parameter 1 "Iterations" "5" 0',
        'Effect "AutoExposure" 1 7', 'Parameter 0 "Key Value" "0.18" 0', 'Parameter 0 "Min Exposure" "0.1" 0',
        'Parameter 0 "Max Exposure" "32" 0', 'Parameter 0 "Adapt Up" "0.05" 0', 'Parameter 0 "Adapt Down" "0.05" 0',
        'Parameter 0 "Low Light EV" "0.2" 0', 'Parameter 0 "Low Light Darkening" "1" 0',
        'Effect "ToneMapping" 1 3', 'Parameter 0 "Exposure" "1" 0', 'Parameter 0 "Gamma" "2.2" 0',
        'Parameter 4 "Curve" "1" 3 "ACES" "AgX" "PBR Neutral"',
        'Effect "FXAA" 1 0', ""]))


def build_models() -> None:
    """The procedural meshes and their import settings: safe to re-run on an edited project (`--models`)."""
    glb("Assets/Models/GrassShort.glb", GRASS_SHORT, VEGETATION_IMPORT)
    glb("Assets/Models/GrassTall.glb", GRASS_TALL, VEGETATION_IMPORT)
    for i, colour in enumerate(("yellow", "white", "violet")):
        glb(f"Assets/Models/Flowers{colour.title()}.glb", flower_clump(40 + i, colour), VEGETATION_IMPORT)
    glb("Assets/Models/TreeTrunk.glb", TREE.trunk, VEGETATION_IMPORT)
    glb("Assets/Models/TreeCrown.glb", TREE.canopy, VEGETATION_IMPORT)
    glb("Assets/Models/Shrub.glb", shrub(31), VEGETATION_IMPORT)
    glb("Assets/Models/Rock.glb", rock(51), VEGETATION_IMPORT)


PROJECT ="".join(line + "\n" for line in [
    "PLUTOPROJECT\t3",
    "NAME\tWind Fields",
    "ASSET_DIR\tAssets",
    "STARTUP_SCENE\tproject://Scenes/WindFields.plutoscene",
    "BUILD_USED_ASSETS_ONLY\t0",
    # No scripts: an empty assembly lets the runtime start without one.
    "SCRIPT_ASSEMBLY\t",
    "LOADING_ACCENT\t0.55\t0.7\t0.35",
    "QUALITY_PRESET\t0\t1\t512\t1\t40\t1\t0\t0\t0\t0\t0\t0\t0",
    "QUALITY_PRESET\t1\t1\t1024\t2\t80\t1\t1\t0\t0\t0\t1\t0\t0",
    "QUALITY_PRESET\t2\t1\t2048\t4\t150\t0\t1\t1\t1\t1\t1\t0\t0",
    "QUALITY_PRESET\t3\t1\t4096\t4\t300\t0\t1\t1\t1\t1\t1\t1\t1",
    "WINDOW_TITLE\tPlutoGE Wind Fields",
    "WINDOW_SIZE\t1600\t900",
    "VSYNC\t1",
    "GRAPHICS_API\tVulkan",
    "RUNTIME_UPSCALER\tNone",
    "RUNTIME_UPSCALER_QUALITY\tQuality",
    "RUNTIME_RENDER_SCALE\t1",
    "EDITOR_CAMERA_POSITION\t-22\t14\t58",
    "EDITOR_CAMERA_SPEED\t12",
    "EDITOR_CAMERA_ROTATION\t-20\t-4",
    "EDITOR_CAMERA_LENS\t55\t0.1\t1200",
    "EDITOR_CAMERA_POST_PROCESS_PRESET\tproject://PostProcessing/Meadow.plutopostprocess",
])


def main() -> None:
    import sys
    if "--models" in sys.argv[1:]:
        # Only the meshes and their import settings: keeps scene, material and post-process edits made in the editor.
        build_models()
        return
    build_assets()
    write("Assets/Scenes/WindFields.plutoscene", build_scene())
    # The editor rewrites the project file (asset catalogue, editor camera), so it is only created once.
    if not (ROOT / "WindFields.plutoproject").exists():
        write("WindFields.plutoproject", PROJECT)
    write(".gitignore", "Captures/\n__pycache__/\n.pluto-import.lock\nLibrary/\n.plutoge-cache/\n")
    print(f"Grass {GRASS_SHORT_HEIGHT} m / {GRASS_TALL_HEIGHT} m tall; tree {TREE_HEIGHT} m")


if __name__ == "__main__":
    main()
