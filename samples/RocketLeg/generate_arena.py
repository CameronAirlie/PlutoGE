"""Regenerate RocketLeg's procedural meshes and generated scene entities (Python 3).

Run from anywhere: ``python samples/RocketLeg/generate_arena.py``.

Meshes (Assets/Models):
  ArenaBanks / ArenaShell   stadium surfaces; static Mesh colliders use these exact
                            triangles through Bullet's concave BVH. Banks have 32
                            segments, plan-view corners 24.
  FieldLines                floor markings (visual only)
  ArenaTrimBlue / Orange    emissive light strips along each team's half of the walls
  CarBody / CarGlass / CarTrim   the low-poly "Breaker" car, in metres, nose towards -Z
  BallPanels / BallAccents  a panelled sphere (truncated icosahedron) of radius ~0.5

Scene (Assets/Scenes/Main.plutoscene): every entity this script creates carries the
"generated" tag and is replaced on the next run; all other entities (gameplay objects,
cameras, user additions) are kept with their tuning. Missing materials are created with
defaults; existing materials are never overwritten, so editor tweaks survive.
"""
from pathlib import Path
import math
import json
import struct
import re

ROOT = Path(__file__).resolve().parent
ASSETS = ROOT / "Assets"
GENERATED_TAG = "generated"

# Stadium plan, mirrored in Assets/Scripts/Core/ArenaLayout.cs (keep them in step).
# The flat floor is a rounded rectangle (half extents FLOOR_HALF_LENGTH x
# FLOOR_HALF_WIDTH, plan corners of CORNER_RADIUS). Banks of BANK_RADIUS rise to
# vertical walls, which curve into the ceiling from WALL_TOP. Goal mouths sit in
# the end walls at x = +/-GOAL_LINE.
FLOOR_HALF_LENGTH = 60.0
FLOOR_HALF_WIDTH = 41.0
CORNER_RADIUS = 10.0
BANK_RADIUS = 7.0
WALL_TOP = 23.0
CEILING = WALL_TOP + BANK_RADIUS
GOAL_LINE = FLOOR_HALF_LENGTH + BANK_RADIUS
GOAL_HALF_WIDTH = 12.0
GOAL_HEIGHT = 9.0
GOAL_DEPTH = 10.0


# ---- Mesh writing --------------------------------------------------------------------

def write_glb(name, vertices, normals, faces):
    """Writes one primitive (positions, normals, uint32 triangles) as Models/<name>.glb."""
    positions = b"".join(struct.pack("<3f", *v) for v in vertices)
    normal_data = b"".join(struct.pack("<3f", *n) for n in normals)
    indices = b"".join(struct.pack("<3I", *f) for f in faces)
    binary = positions + normal_data + indices
    gltf = {
        "asset": {"version": "2.0", "generator": "RocketLeg generate_arena.py"},
        "scene": 0, "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": name}],
        "meshes": [{"name": name, "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1}, "indices": 2}]}],
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": len(positions), "target": 34962},
            {"buffer": 0, "byteOffset": len(positions), "byteLength": len(normal_data), "target": 34962},
            {"buffer": 0, "byteOffset": len(positions)+len(normal_data), "byteLength": len(indices), "target": 34963}],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": len(vertices), "type": "VEC3",
             "min": [min(v[i] for v in vertices) for i in range(3)], "max": [max(v[i] for v in vertices) for i in range(3)]},
            {"bufferView": 1, "componentType": 5126, "count": len(vertices), "type": "VEC3"},
            {"bufferView": 2, "componentType": 5125, "count": len(faces)*3, "type": "SCALAR"}]
    }
    metadata = json.dumps(gltf, separators=(",", ":")).encode()
    metadata += b" " * (-len(metadata) % 4)
    data = struct.pack("<III", 0x46546c67, 2, 28+len(metadata)+len(binary))
    data += struct.pack("<II", len(metadata), 0x4e4f534a) + metadata
    data += struct.pack("<II", len(binary), 0x004e4942) + binary
    folder = ASSETS / "Models"
    folder.mkdir(exist_ok=True)
    (folder / (name + ".glb")).write_bytes(data)


def sub(a, b): return [a[i]-b[i] for i in range(3)]
def add(a, b): return [a[i]+b[i] for i in range(3)]
def scale(a, s): return [x*s for x in a]
def cross(u, v): return [u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]]
def dot(u, v): return sum(u[i]*v[i] for i in range(3))
def normalize(a):
    length = math.sqrt(dot(a, a)) or 1.0
    return [x/length for x in a]


def smooth_normals(vertices, faces):
    normals = [[0.0]*3 for _ in vertices]
    for f in faces:
        a, b, c = [vertices[i] for i in f]
        n = cross(sub(b, a), sub(c, a))
        assert dot(n, n) > 1e-12, "Degenerate triangle"
        for index in f:
            normals[index] = add(normals[index], n)
    return [normalize(n) for n in normals]


class FlatMesh:
    """Accumulates faceted polygons: every polygon gets its own vertices and face normal."""

    def __init__(self):
        self.vertices, self.normals, self.faces = [], [], []

    def polygon(self, points, outward_hint=None):
        """A planar convex polygon. Winding is flipped if needed to face outward_hint."""
        n = normalize(cross(sub(points[1], points[0]), sub(points[2], points[0])))
        if outward_hint is not None and dot(n, outward_hint) < 0:
            points = points[::-1]
            n = scale(n, -1)
        start = len(self.vertices)
        self.vertices += [list(p) for p in points]
        self.normals += [n] * len(points)
        self.faces += [(start, start+i, start+i+1) for i in range(1, len(points)-1)]

    def box(self, lo, hi):
        x0, y0, z0 = lo
        x1, y1, z1 = hi
        centre = [(x0+x1)/2, (y0+y1)/2, (z0+z1)/2]
        corners = [[x, y, z] for x in (x0, x1) for y in (y0, y1) for z in (z0, z1)]
        for axis in range(3):
            for side in (0, 1):
                quad = [c for c in corners if c[axis] == (lo, hi)[side][axis]]
                # Order the four corners around the face.
                u, v = [a for a in range(3) if a != axis]
                quad.sort(key=lambda c: math.atan2(c[v]-centre[v], c[u]-centre[u]))
                self.polygon(quad, [(-1, 1)[side] if a == axis else 0 for a in range(3)])

    def loft(self, sections):
        """Joins equal-length convex rings into a closed hull, capping both ends."""
        centroid = lambda points: [sum(p[a] for p in points)/len(points) for a in range(3)]
        centres = [centroid(s) for s in sections]
        centre = centroid(centres)
        count = len(sections[0])
        for j in range(len(sections)-1):
            axis = centroid([centres[j], centres[j+1]])
            for i in range(count):
                k = (i+1) % count
                quad = [sections[j][i], sections[j][k], sections[j+1][k], sections[j+1][i]]
                # Outward is away from the hull's axis between these two rings.
                self.polygon(quad, sub(centroid(quad), axis))
        for ring, ring_centre in ((sections[0], centres[0]), (sections[-1], centres[-1])):
            self.polygon(list(ring), sub(ring_centre, centre))

    def write(self, name):
        write_glb(name, self.vertices, self.normals, self.faces)


# ---- Stadium -------------------------------------------------------------------------

def ring(offset, y, split_goals=True):
    """A rounded rectangle offset outward from the floor edge; corner centres stay fixed."""
    radius = CORNER_RADIUS + offset
    cx0, cz0 = FLOOR_HALF_LENGTH - CORNER_RADIUS, FLOOR_HALF_WIDTH - CORNER_RADIUS
    points = []
    for cx, cz, start in [(cx0, cz0, 0), (-cx0, cz0, 90), (-cx0, -cz0, 180), (cx0, -cz0, 270)]:
        for i in range(25):
            a = math.radians(start + i * 90 / 24)
            points.append((cx + radius * math.cos(a), y, cz + radius * math.sin(a)))
        # Split end lines at goalposts, allowing a precise eight-unit half-mouth.
        if split_goals and cx < 0 < cz:
            points.extend([(cx-radius, y, GOAL_HALF_WIDTH), (cx-radius, y, -GOAL_HALF_WIDTH)])
        if split_goals and cz < 0 < cx:
            points.extend([(cx+radius, y, -GOAL_HALF_WIDTH), (cx+radius, y, GOAL_HALF_WIDTH)])
    return points


def in_goal_mouth(a, b, top):
    return top <= GOAL_HEIGHT and abs(a[0]) > FLOOR_HALF_LENGTH - 1 and abs(b[0]) > FLOOR_HALF_LENGTH - 1 and abs(a[2]) <= GOAL_HALF_WIDTH and abs(b[2]) <= GOAL_HALF_WIDTH


def write_stadium_mesh(name, rows, ceiling=False):
    vertices = [v for row in rows for v in row]
    count = len(rows[0])
    faces = []
    for j in range(len(rows)-1):
        for i in range(count):
            k = (i+1) % count
            if in_goal_mouth(rows[j][i], rows[j][k], rows[j+1][i][1]):
                continue
            # Counterclockwise perimeter in XZ: these faces point into the arena.
            q = (j*count+i, j*count+k, (j+1)*count+k, (j+1)*count+i)
            faces.extend([(q[0], q[1], q[2]), (q[0], q[2], q[3])])
    if ceiling:
        centre = len(vertices)
        vertices.append((0, CEILING, 0))
        start = (len(rows)-1)*count
        faces.extend([(centre, start+i, start+(i+1) % count) for i in range(count)])
    write_glb(name, vertices, smooth_normals(vertices, faces), faces)


def split_at_halfway(points):
    """Inserts a point wherever a closed ring crosses x = 0, so halves can be told apart."""
    result = []
    for i, a in enumerate(points):
        b = points[(i+1) % len(points)]
        result.append(a)
        if (a[0] < 0) != (b[0] < 0) and abs(a[0]-b[0]) > 1e-6:
            t = a[0] / (a[0]-b[0])
            result.append((0.0, a[1] + (b[1]-a[1])*t, a[2] + (b[2]-a[2])*t))
    return result


def write_trims():
    """Emissive bands on the vertical walls, one mesh per team half."""
    # (bottom, top, crosses goal mouths): one just above the banks, one under the roof curve.
    bands = [(BANK_RADIUS + 0.25, BANK_RADIUS + 0.65, True), (WALL_TOP - 0.8, WALL_TOP - 0.4, False)]
    meshes = {"Blue": FlatMesh(), "Orange": FlatMesh()}
    inset = BANK_RADIUS - 0.04  # just inside the wall face
    for bottom, top, skip_goals in bands:
        lower = split_at_halfway(ring(inset, bottom))
        upper = split_at_halfway(ring(inset, top))
        for i in range(len(lower)):
            k = (i+1) % len(lower)
            a, b = lower[i], lower[k]
            if math.dist(a, b) < 1e-5 or (skip_goals and in_goal_mouth(a, b, bottom)):
                continue
            team = "Blue" if (a[0] + b[0]) < 0 else "Orange"
            mid = [(a[0]+b[0])/2, 0, (a[2]+b[2])/2]
            meshes[team].polygon([a, b, upper[k], upper[i]], scale(normalize(mid), -1))
    for team, mesh in meshes.items():
        mesh.write("ArenaTrim" + team)


def write_field_lines():
    mesh = FlatMesh()
    y, up = 0.012, [0, 1, 0]
    def quad(x0, z0, x1, z1):
        mesh.polygon([[x0, y, z0], [x1, y, z0], [x1, y, z1], [x0, y, z1]], up)
    def annulus(cx, cz, inner, outer, segments=72):
        for i in range(segments):
            a0, a1 = 2*math.pi*i/segments, 2*math.pi*(i+1)/segments
            mesh.polygon([[cx+inner*math.cos(a0), y, cz+inner*math.sin(a0)], [cx+outer*math.cos(a0), y, cz+outer*math.sin(a0)],
                          [cx+outer*math.cos(a1), y, cz+outer*math.sin(a1)], [cx+inner*math.cos(a1), y, cz+inner*math.sin(a1)]], up)
    quad(-0.15, -(FLOOR_HALF_WIDTH-0.4), 0.15, FLOOR_HALF_WIDTH-0.4)   # halfway line
    annulus(0, 0, 9.6, 10.0)                     # centre circle
    annulus(0, 0, 0.0001, 0.45, 24)              # centre spot
    w, box_half = 0.3, GOAL_HALF_WIDTH + 6
    for sign in (-1, 1):
        # Goal box from the floor edge 12 m out, 6 m wider than the mouth each side.
        edge, depth = sign*(FLOOR_HALF_LENGTH-0.4), sign*(FLOOR_HALF_LENGTH-12)
        x0, x1 = sorted((edge, depth))
        quad(x0, -box_half-w/2, x1, -box_half+w/2)
        quad(x0, box_half-w/2, x1, box_half+w/2)
        quad(depth-w/2, -box_half, depth+w/2, box_half)
    # Boundary line just inside the floor edge.
    inner, outer = ring(-0.9, y, split_goals=False), ring(-0.6, y, split_goals=False)
    for i in range(len(inner)):
        k = (i+1) % len(inner)
        if math.dist(inner[i], inner[k]) > 1e-5:
            mesh.polygon([inner[i], outer[i], outer[k], inner[k]], up)
    mesh.write("FieldLines")


# ---- Car: "Breaker", an original low wedge with a set-back cabin --------------------

def car_section(z, half_width, bottom, top, shoulder=0.55, taper=0.86):
    """A bevelled cross-section ring at depth z (counterclockwise seen from the front)."""
    mid = bottom + (top-bottom)*shoulder
    return [[half_width, bottom, z], [half_width, mid, z], [half_width*taper, top, z],
            [-half_width*taper, top, z], [-half_width, mid, z], [-half_width, bottom, z]]


def write_car():
    body = FlatMesh()
    # z, half width, bottom, top: nose (-Z) to tail (+Z).
    profile = [(-1.10, 0.46, -0.07, 0.00), (-1.02, 0.56, -0.12, 0.09), (-0.62, 0.60, -0.12, 0.19),
               (-0.22, 0.60, -0.12, 0.25), (0.55, 0.60, -0.12, 0.29), (0.92, 0.59, -0.10, 0.30),
               (1.04, 0.54, -0.04, 0.25)]
    body.loft([car_section(*p) for p in profile])
    # Flared wheel arches over each wheel.
    for x in (-1, 1):
        for z in (-0.72, 0.72):
            lo, hi = sorted((x*0.52, x*0.68))
            body.box([lo, 0.02, z-0.36], [hi, 0.22, z+0.36])
    body.write("CarBody")

    glass = FlatMesh()
    cabin = [(-0.24, 0.50, 0.24, 0.26, 0.2, 0.98), (0.02, 0.43, 0.24, 0.60, 0.15, 0.82),
             (0.42, 0.41, 0.26, 0.61, 0.15, 0.80), (0.80, 0.48, 0.27, 0.32, 0.2, 0.95)]
    glass.loft([car_section(z, w, b, t, s, tp) for z, w, b, t, s, tp in cabin])
    glass.write("CarGlass")

    trim = FlatMesh()
    trim.box([-0.60, -0.16, -1.14], [0.60, -0.09, -0.92])   # front splitter
    trim.box([-0.62, -0.15, -0.36], [-0.58, -0.05, 0.36])   # side skirts
    trim.box([0.58, -0.15, -0.36], [0.62, -0.05, 0.36])
    trim.box([-0.56, -0.12, 0.98], [0.56, 0.02, 1.07])      # rear diffuser
    trim.box([-0.38, 0.29, 0.86], [-0.32, 0.46, 0.94])      # spoiler struts
    trim.box([0.32, 0.29, 0.86], [0.38, 0.46, 0.94])
    trim.box([-0.64, 0.45, 0.80], [0.64, 0.50, 1.04])       # wing
    trim.box([-0.66, 0.36, 0.80], [-0.62, 0.52, 1.04])      # wing end plates
    trim.box([0.62, 0.36, 0.80], [0.66, 0.52, 1.04])
    trim.write("CarTrim")


# ---- Ball: panelled sphere -----------------------------------------------------------

def write_ball(radius=0.506, gap=0.93):
    phi = (1 + 5 ** 0.5) / 2
    ico = [normalize(v) for v in [(-1, phi, 0), (1, phi, 0), (-1, -phi, 0), (1, -phi, 0), (0, -1, phi), (0, 1, phi),
                                  (0, -1, -phi), (0, 1, -phi), (phi, 0, -1), (phi, 0, 1), (-phi, 0, -1), (-phi, 0, 1)]]
    edge = min(math.dist(ico[0], v) for v in ico[1:])
    neighbours = [[j for j in range(12) if j != i and math.dist(ico[i], ico[j]) < edge*1.01] for i in range(12)]
    tris = sorted({tuple(sorted((i, j, k))) for i in range(12) for j in neighbours[i] for k in neighbours[i]
                   if j < k and k in neighbours[j]})
    lerp = lambda a, b, t: [a[i]+(b[i]-a[i])*t for i in range(3)]

    def around(points, axis):
        # Orders polygon corners around their centre, counterclockwise seen from outside.
        u = normalize(cross(axis, [0.3, 0.9, 0.1] if abs(axis[1]) < 0.9 else [1, 0, 0]))
        v = cross(axis, u)
        return sorted(points, key=lambda p: math.atan2(dot(p, v), dot(p, u)))

    pentagons = [around([lerp(ico[i], ico[j], 1/3) for j in neighbours[i]], ico[i]) for i in range(12)]
    hexagons = []
    for a, b, c in tris:
        centre = normalize(add(add(ico[a], ico[b]), ico[c]))
        pts = [lerp(ico[x], ico[y], t) for x, y in ((a, b), (b, c), (c, a)) for t in (1/3, 2/3)]
        hexagons.append(around(pts, centre))

    def panel_mesh(name, polygons):
        vertices, faces = [], []
        for poly in polygons:
            centre = [sum(p[i] for p in poly)/len(poly) for i in range(3)]
            shrunk = [lerp(centre, p, gap) for p in poly]
            # Fan, then split each triangle in four so the panel bulges with the sphere.
            for i in range(len(shrunk)):
                a, b, c = centre, shrunk[i], shrunk[(i+1) % len(shrunk)]
                ab, bc, ca = lerp(a, b, .5), lerp(b, c, .5), lerp(c, a, .5)
                for tri in ((a, ab, ca), (ab, b, bc), (ca, bc, c), (ab, bc, ca)):
                    start = len(vertices)
                    vertices += [scale(normalize(p), radius) for p in tri]
                    faces.append((start, start+1, start+2))
        normals = [normalize(v) for v in vertices]
        # Make every triangle face outward.
        faces = [f if dot(cross(sub(vertices[f[1]], vertices[f[0]]), sub(vertices[f[2]], vertices[f[0]])), vertices[f[0]]) > 0
                 else (f[0], f[2], f[1]) for f in faces]
        write_glb(name, vertices, normals, faces)

    panel_mesh("BallPanels", hexagons)
    panel_mesh("BallAccents", pentagons)


# ---- Materials -----------------------------------------------------------------------

LIT = "engine://builtin/shadergraph/default-lit"

MATERIALS = {
    "FieldBlue": dict(color="0.05,0.19,0.2,1", roughness=0.78),
    "FieldOrange": dict(color="0.2,0.11,0.045,1", roughness=0.78),
    "FieldLines": dict(color="0.92,0.95,1,1", roughness=0.6, emission="0.55,0.6,0.7", shadow=False),
    "TrimBlue": dict(color="0.1,0.45,1,1", roughness=0.4, emission="0.35,1.6,5"),
    "TrimOrange": dict(color="1,0.42,0.08,1", roughness=0.4, emission="5,1.7,0.25"),
    "GoalGlassBlue": dict(color="0.12,0.36,0.9,0.3", roughness=0.08, glass=True, attenuation="0.45,0.65,1"),
    "GoalGlassOrange": dict(color="0.95,0.4,0.12,0.3", roughness=0.08, glass=True, attenuation="1,0.7,0.45"),
    "CarPaintBlue": dict(color="0.03,0.22,0.85,1", metallic=0.55, roughness=0.22),
    "CarPaintOrange": dict(color="0.95,0.3,0.03,1", metallic=0.55, roughness=0.22),
    "CarGlass": dict(color="0.03,0.04,0.06,1", metallic=0.7, roughness=0.06),
    "CarTrim": dict(color="0.05,0.055,0.06,1", metallic=0.3, roughness=0.5),
    "Exhaust": dict(color="0.12,0.12,0.13,1", metallic=0.9, roughness=0.3, emission="0.25,0.08,0.02"),
    "Rim": dict(color="0.62,0.64,0.68,1", metallic=1.0, roughness=0.25),
    "BallCore": dict(color="0.05,0.06,0.08,1", metallic=0.2, roughness=0.5),
    "BallPanels": dict(color="0.86,0.88,0.92,1", metallic=0.1, roughness=0.35),
    "BallAccents": dict(color="0.2,0.85,1,1", metallic=0.1, roughness=0.3, emission="0.25,1.1,1.6"),
    "BallMarker": dict(color="1,1,1,0.55", roughness=1.0, emission="0.9,0.9,1", blend=True, shadow=False),
}


def ensure_materials():
    folder = ASSETS / "Materials"
    for name, m in MATERIALS.items():
        path = folder / f"{name}.plutomaterial"
        if path.exists():
            continue
        glass = m.get("glass", False)
        lines = [f"Color={m['color']}",
                 f"SurfaceType={'Glass' if glass else 'Standard'}",
                 f"AlphaMode={'Blend' if glass or m.get('blend') else 'Opaque'}",
                 f"CastsShadow={'true' if m.get('shadow', not glass) else 'false'}",
                 f"TwoSided={'true' if glass else 'false'}",
                 f"Metallic={m.get('metallic', 0)}",
                 f"Roughness={m['roughness']}",
                 f"Emission={m.get('emission', '0,0,0')}"]
        if glass:
            lines += ["Transmission=0.8", "Ior=1.5", "Thickness=0.5", f"AttenuationColor={m['attenuation']}", "AttenuationDistance=1"]
        lines.append(f"ShaderGraph={LIT}")
        path.write_text("\n".join(lines) + "\n", newline="\n")


# ---- Scene entities ------------------------------------------------------------------

CUBE, CYLINDER, SPHERE = "engine://builtin/mesh/cube", "engine://builtin/mesh/cylinder", "engine://builtin/mesh/sphere"
vec = lambda value: ",".join(f"{v:.6f}" for v in value)


class Entities:
    def __init__(self, first_id):
        self.next_id = first_id
        self.text = ""

    def add(self, name, position, scale=(1, 1, 1), rotation=(0, 0, 0), parent=0, tags=(), components=()):
        index = self.next_id
        self.next_id += 1
        all_tags = (GENERATED_TAG,) + tuple(tags)
        self.text += f"ENTITY\t{index}\t{parent}\t1\t{name}\t{vec(position)}\t{vec(rotation)}\t{vec(scale)}\n"
        self.text += f"TAGS\t{index}\t{len(all_tags)}\t" + "\t".join(all_tags) + "\n"
        for component in components:
            self.text += component(index)
        return index


def mesh(model, material, static=True, visible=True):
    def write(index):
        return "\n".join([
            f"COMPONENT\t{index}\tMeshComponent\t1", f"PROPERTY\tStatic\t4\t{'true' if static else 'false'}\t0",
            f"PROPERTY\tVisible\t4\t{'true' if visible else 'false'}\t0", "PROPERTY\tSubmeshIndex\t1\t-1\t0",
            f"PROPERTY\tModelAssetId\t2\t{model}\t0", "PROPERTY\tModelObjectId\t2\t0\t0",
            f"PROPERTY\tMeshAssetReference\t2\t{model}\t0",
            f"PROPERTY\tMaterialSlots.0.MaterialAsset\t2\tproject://Materials/{material}.plutomaterial\t0", "END_COMPONENT"]) + "\n"
    return write


def collider(shape):
    shapes = {"Box": 0, "Mesh": 4}
    def write(index):
        return "\n".join([
            f"COMPONENT\t{index}\tColliderComponent\t1",
            f"PROPERTY\tShape\t6\t{shapes[shape]}\t5\tBox\tSphere\tCapsule\tTerrain\tMesh",
            "PROPERTY\tSize\t3\t1,1,1\t0", "END_COMPONENT"]) + "\n"
    return write


def script(name, **fields):
    types = {bool: 4, int: 1, float: 0, str: 2}
    def write(index):
        lines = [f"COMPONENT\t{index}\tScriptComponent\t1", f"PROPERTY\tSource\t2\tRocketLeg.Scripts.{name}\t0"]
        for key, value in fields.items():
            text = ("true" if value else "false") if isinstance(value, bool) else f"{value:.6f}" if isinstance(value, float) else str(value)
            lines.append(f"PROPERTY\t{key}\t{types[type(value)]}\t{text}\t0")
        return "\n".join(lines + ["END_COMPONENT"]) + "\n"
    return write


def point_light(colour, intensity):
    def write(index):
        return "\n".join([
            f"COMPONENT\t{index}\tLightComponent\t1", f"PROPERTY\tColor\t3\t{vec(colour)}\t0",
            f"PROPERTY\tIntensity\t0\t{intensity:.6f}\t0", "PROPERTY\tCastsShadows\t4\tfalse\t0",
            "PROPERTY\tLightType\t6\t0\t3\tPoint\tDirectional\tSpot", "PROPERTY\tStatic\t4\tfalse\t0", "END_COMPONENT"]) + "\n"
    return write


def particles(asset):
    def write(index):
        return (f"COMPONENT\t{index}\tParticleSystemComponent\t1\n"
                f"PROPERTY\tParticleSystemAsset\t2\tproject://Effects/{asset}.plutoparticles\t0\nEND_COMPONENT\n")
    return write


# Boost pads: big pads fill the tank, small pads add a little. Blue half only; mirrored.
BIG_PADS = [(-54, -35), (-54, 35), (0, -37)]
SMALL_PADS = [(-52, 0), (-40, -15), (-40, 15), (-26, -27), (-26, 27), (-26, 0), (-11, -14), (-11, 14), (-14, -36),
              (-14, 36), (-46, -26), (-46, 26)]


def mirrored(points):
    seen, result = set(), []
    for x, z in points + [(-x, -z) for x, z in points]:
        if (x, z) not in seen:
            seen.add((x, z))
            result.append((x, z))
    return result


def stadium_entities(e):
    # The collider stays a single box; two visible halves give each team its colour.
    length, width = 2*FLOOR_HALF_LENGTH, 2*FLOOR_HALF_WIDTH
    e.add("Arena Floor", (0, -0.25, 0), (length, 0.5, width), tags=("ground",),
          components=(collider("Box"), mesh(CUBE, "FieldBlue", visible=False)))
    e.add("Blue Half", (-FLOOR_HALF_LENGTH/2, -0.25, 0), (FLOOR_HALF_LENGTH, 0.5, width), components=(mesh(CUBE, "FieldBlue"),))
    e.add("Orange Half", (FLOOR_HALF_LENGTH/2, -0.25, 0), (FLOOR_HALF_LENGTH, 0.5, width), components=(mesh(CUBE, "FieldOrange"),))
    e.add("Field Lines", (0, 0, 0), components=(mesh("project://Models/FieldLines.glb", "FieldLines"),))
    e.add("Concave Lower Banks", (0, 0, 0), tags=("ground",), components=(collider("Mesh"), mesh("project://Models/ArenaBanks.glb", "Wall")))
    e.add("Translucent Walls and Ceiling", (0, 0, 0), tags=("ground",),
          components=(collider("Mesh"), mesh("project://Models/ArenaShell.glb", "StadiumGlass")))
    # A solid slab over the thin roof mesh: a car leaving the roof curve at top
    # speed could otherwise tunnel through it in one physics step.
    e.add("Roof Slab", (0, CEILING + 1, 0), (length + 24, 2, width + 24), tags=("ground",),
          components=(collider("Box"), mesh(CUBE, "StadiumGlass", visible=False)))
    e.add("Blue Wall Trim", (0, 0, 0), components=(mesh("project://Models/ArenaTrimBlue.glb", "TrimBlue"),))
    e.add("Orange Wall Trim", (0, 0, 0), components=(mesh("project://Models/ArenaTrimOrange.glb", "TrimOrange"),))

    for sign, team in [(-1, "Blue"), (1, "Orange")]:
        glass = "GoalGlass" + team
        # The goal floor starts under the bank cut-out in front of the mouth; the
        # side walls start there too so nothing slips beneath the cut bank edges.
        start, mouth, back = FLOOR_HALF_LENGTH - 1, GOAL_LINE, GOAL_LINE + GOAL_DEPTH
        half_w, h = GOAL_HALF_WIDTH, GOAL_HEIGHT
        for name, pos, size, material in [
            ("Goal Floor", (sign*(start+back)/2, -0.25, 0), (back-start, 0.5, 2*half_w), "Field" + team),
            ("Goal Back", (sign*(back+0.25), h/2, 0), (0.5, h, 2*half_w+0.5), glass),
            ("Goal North", (sign*(start+back)/2, h/2, -half_w-0.25), (back-start+0.5, h, 0.5), glass),
            ("Goal South", (sign*(start+back)/2, h/2, half_w+0.25), (back-start+0.5, h, 0.5), glass),
            ("Goal Roof", (sign*(mouth+back)/2, h+0.25, 0), (GOAL_DEPTH+0.5, 0.5, 2*half_w+0.5), glass),
            ("Goal Crossbar", (sign*mouth, h+0.15, 0), (0.35, 0.3, 2*half_w+0.5), "Goal" + team),
            ("Goal Post North", (sign*mouth, h/2, -half_w-0.15), (0.35, h, 0.3), "Goal" + team),
            ("Goal Post South", (sign*mouth, h/2, half_w+0.15), (0.35, h, 0.3), "Goal" + team),
        ]:
            e.add(f"{team} {name}", pos, size, tags=("ground", team.lower()), components=(collider("Box"), mesh(CUBE, material)))
        colour = (0.25, 0.55, 1.0) if team == "Blue" else (1.0, 0.5, 0.15)
        e.add(f"{team} Goal Light", (sign*(GOAL_LINE+GOAL_DEPTH/2), h/2, 0), components=(point_light(colour, 90.0),))

    for x, z in mirrored(BIG_PADS):
        pad = e.add("Big Boost Pad", (x, 0.06, z), (2.6, 0.12, 2.6), tags=("boost-pad",),
                    components=(mesh(CYLINDER, "BoostPad", static=False), script("BoostPad", big=True)))
        # Children inherit the pad's flat scale; undo it so the orb stays round.
        e.add("Orb", (0, 8.5, 0), (0.42, 9.0, 0.42), parent=pad, components=(mesh(SPHERE, "BoostPad", static=False),))
    for x, z in mirrored(SMALL_PADS):
        e.add("Small Boost Pad", (x, 0.04, z), (1.3, 0.08, 1.3), tags=("boost-pad",),
              components=(mesh(CYLINDER, "BoostPad", static=False), script("BoostPad", big=False)))


def car_rig(e, car_id, team, chassis_scale=(1.2, 0.4, 2.0)):
    """Visual children for a car. The root cancels the chassis scale so children are in metres."""
    root = e.add("Visual Root", (0, 0, 0), tuple(1/s for s in chassis_scale), parent=car_id)
    paint = "CarPaint" + team
    e.add("Body", (0, 0, 0), parent=root, components=(mesh("project://Models/CarBody.glb", paint, static=False),))
    e.add("Glass", (0, 0, 0), parent=root, components=(mesh("project://Models/CarGlass.glb", "CarGlass", static=False),))
    e.add("Trim", (0, 0, 0), parent=root, components=(mesh("project://Models/CarTrim.glb", "CarTrim", static=False),))
    e.add("Exhaust", (0, 0.06, 1.05), (0.2, 0.12, 0.2), (90, 0, 0), parent=root, components=(mesh(CYLINDER, "Exhaust", static=False),))
    # Particle cones emit along local +Y; tip them to point out of the tail (+Z).
    e.add("Boost Flame", (0, 0.06, 1.12), rotation=(90, 0, 0), parent=root, components=(particles("BoostFlame"),))
    e.add("Supersonic Trail", (0, 0.12, 1.0), rotation=(90, 0, 0), parent=root, components=(particles("SupersonicTrail"),))
    for name, x, z in [("Wheel FL", -0.56, -0.72), ("Wheel FR", 0.56, -0.72), ("Wheel RL", -0.56, 0.72), ("Wheel RR", 0.56, 0.72)]:
        wheel = e.add(name, (x, -0.02, z), (0.64, 0.32, 0.64), (0, 0, 90), parent=root, components=(mesh(CYLINDER, "Tyre", static=False),))
        # Slightly wider than the tyre so the hub face shows on both sides.
        e.add("Rim", (0, 0, 0), (0.6, 1.04, 0.6), parent=wheel, components=(mesh(CYLINDER, "Rim", static=False),))


def ball_rig(e, ball_id):
    e.add("Ball Panels", (0, 0, 0), parent=ball_id, components=(mesh("project://Models/BallPanels.glb", "BallPanels", static=False),))
    e.add("Ball Accents", (0, 0, 0), parent=ball_id, components=(mesh("project://Models/BallAccents.glb", "BallAccents", static=False),))


def effect_entities(e):
    e.add("Ball Marker", (0, 0.02, 0), (2.4, 0.01, 2.4), components=(mesh(CYLINDER, "BallMarker", static=False), script("BallMarker")))
    explosion = e.add("Goal Explosion", (0, -50, 0), components=(particles("GoalExplosion"), script("GoalExplosion")))
    e.add("Flash", (0, 0, 0), parent=explosion, components=(point_light((1, 1, 1), 0.0),))
    e.add("Demolition Effect", (0, -50, 0), components=(particles("DemolitionBurst"),))


def is_generated(block, index, parent):
    first = block.splitlines()
    tags = next((line.split("\t")[3:] for line in first[1:3] if line.startswith("TAGS\t")), [])
    if GENERATED_TAG in tags:
        return True
    # Entities made by earlier versions of this script, before the tag existed.
    return (index < 20 and parent == 0) or (100 <= index < 200 and parent == 0) or index in (21, 23) or 200 <= index <= 207


def main():
    b = BANK_RADIUS
    lower = [ring(b*math.sin(i*math.pi/64), b*(1-math.cos(i*math.pi/64))) for i in range(33)]
    # The goal-height row lets the mouth be cut exactly.
    upper = [ring(b, b), ring(b, GOAL_HEIGHT), ring(b, WALL_TOP)]
    upper += [ring(b*math.cos(i*math.pi/64), WALL_TOP+b*math.sin(i*math.pi/64)) for i in range(1, 33)]
    write_stadium_mesh("ArenaBanks", lower)
    write_stadium_mesh("ArenaShell", upper, ceiling=True)
    write_trims()
    write_field_lines()
    write_car()
    write_ball()
    ensure_materials()

    scene_path = ASSETS / "Scenes/Main.plutoscene"
    blocks = re.split(r"(?=^ENTITY\t)", scene_path.read_text(), flags=re.M)
    retained, names = [], {}
    for block in blocks[1:]:
        fields = block.splitlines()[0].split("\t")
        index, parent = int(fields[1]), int(fields[2])
        if is_generated(block, index, parent):
            continue
        names[fields[4]] = index
        retained.append(block)

    e = Entities(1000)
    stadium_entities(e)
    effect_entities(e)
    for car, team in [("Blue Car", "Blue"), ("Orange Car", "Orange")]:
        if car in names:
            car_rig(e, names[car], team)
    if "Ball" in names:
        ball_rig(e, names["Ball"])
    # Generated entities come first so retained children can reference retained parents.
    scene_path.write_text("SCENE\t1\n" + e.text + "".join(retained), newline="\n")

    project = ROOT / "RocketLeg.plutoproject"
    lines = project.read_text().splitlines()
    known = set()
    for i, line in enumerate(lines):
        parts = line.split("\t")
        if len(parts) == 4 and parts[0] == "ASSET" and parts[1].startswith("project://"):
            relative = parts[1][10:]
            known.add(relative)
            asset = ASSETS / relative
            if asset.is_file():
                parts[2] = str(asset.stat().st_size)
                lines[i] = "\t".join(parts)
    kinds = {".glb": "Model", ".plutomaterial": "Material"}
    for folder in ("Models", "Materials"):
        for asset in sorted((ASSETS / folder).iterdir()):
            relative = f"{folder}/{asset.name}"
            if asset.suffix in kinds and relative not in known:
                lines.append(f"ASSET\tproject://{relative}\t{asset.stat().st_size}\t{kinds[asset.suffix]}")
    project.write_text("\n".join(lines) + "\n", newline="\n")


if __name__ == "__main__":
    main()
