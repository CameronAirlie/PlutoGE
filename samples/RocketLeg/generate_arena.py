"""Regenerate the stadium GLB assets and scene geometry using Python 3.

Static Mesh colliders use these exact triangles through Bullet's concave BVH.
The quarter-circle banks have 32 segments; plan-view corners have 24.
"""
from pathlib import Path
import math
import json
import struct
import re

ROOT = Path(__file__).resolve().parent
ASSETS = ROOT / "Assets"


def ring(offset, y):
    # Offset a rounded rectangle: corner centres stay fixed, preserving tangency.
    radius = 3 + offset
    points = []
    for cx, cz, start in [(32, 18, 0), (-32, 18, 90), (-32, -18, 180), (32, -18, 270)]:
        for i in range(25):
            a = math.radians(start + i * 90 / 24)
            points.append((cx + radius * math.cos(a), y, cz + radius * math.sin(a)))
        # Split end lines at goalposts, allowing a precise eight-unit half-mouth.
        if cx == -32 and cz == 18:
            points.extend([(-32-radius, y, 8), (-32-radius, y, -8)])
        if cx == 32 and cz == -18:
            points.extend([(32+radius, y, -8), (32+radius, y, 8)])
    return points


def write_mesh(name, rows, ceiling=False):
    vertices = [v for row in rows for v in row]
    count = len(rows[0])
    faces = []
    for j in range(len(rows)-1):
        for i in range(count):
            k = (i+1) % count
            a, b = rows[j][i], rows[j][k]
            if rows[j+1][i][1] <= 8 and abs(a[0]) > 34 and abs(b[0]) > 34 and abs(a[2]) <= 8 and abs(b[2]) <= 8:
                continue
            # Counterclockwise perimeter in XZ: these faces point into the arena.
            q = (j*count+i, j*count+k, (j+1)*count+k, (j+1)*count+i)
            faces.extend([(q[0], q[1], q[2]), (q[0], q[2], q[3])])
    if ceiling:
        centre = len(vertices)
        vertices.append((0, 22, 0))
        start = (len(rows)-1)*count
        faces.extend([(centre, start+i, start+(i+1)%count) for i in range(count)])
    normals = [[0.0]*3 for _ in vertices]
    for f in faces:
        a, b, c = [vertices[i] for i in f]
        u, v = [b[i]-a[i] for i in range(3)], [c[i]-a[i] for i in range(3)]
        n = [u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]]
        assert sum(x*x for x in n) > 1e-12, "Degenerate stadium triangle"
        for index in f:
            for axis in range(3):
                normals[index][axis] += n[axis]
    normalized = []
    for n in normals:
        length = math.sqrt(sum(x*x for x in n)) or 1
        normalized.append([x/length for x in n])
    positions = b"".join(struct.pack("<3f", *v) for v in vertices)
    normal_data = b"".join(struct.pack("<3f", *n) for n in normalized)
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



def entity(index, name, position, scale, material, mesh="engine://builtin/mesh/cube", collider=True, script=None):
    vec = lambda value: ",".join(f"{v:.6f}" for v in value)
    lines = [f"ENTITY\t{index}\t0\t1\t{name}\t{vec(position)}\t0.000000,0.000000,0.000000\t{vec(scale)}"]
    lines += [f"TAGS\t{index}\t1\t" + ("boost-pad" if script else "ground")]
    if collider:
        lines += [f"COMPONENT\t{index}\tColliderComponent\t1", f"PROPERTY\tShape\t6\t{4 if mesh.endswith('.glb') else 0}\t5\tBox\tSphere\tCapsule\tTerrain\tMesh", "PROPERTY\tSize\t3\t1,1,1\t0", "END_COMPONENT"]
    lines += [f"COMPONENT\t{index}\tMeshComponent\t1", f"PROPERTY\tStatic\t4\t{'false' if script else 'true'}\t0", "PROPERTY\tVisible\t4\ttrue\t0", "PROPERTY\tSubmeshIndex\t1\t-1\t0", f"PROPERTY\tModelAssetId\t2\t{mesh}\t0", "PROPERTY\tModelObjectId\t2\t0\t0", f"PROPERTY\tMeshAssetReference\t2\t{mesh}\t0", f"PROPERTY\tMaterialSlots.0.MaterialAsset\t2\tproject://Materials/{material}.plutomaterial\t0", "END_COMPONENT"]
    if script:
        lines += [f"COMPONENT\t{index}\tScriptComponent\t1", f"PROPERTY\tSource\t2\tRocketLeg.Scripts.{script}\t0", "END_COMPONENT"]
    return "\n".join(lines) + "\n"


def main():
    lower = [ring(5*math.sin(i*math.pi/64), 5*(1-math.cos(i*math.pi/64))) for i in range(33)]
    upper = [ring(5, 5), ring(5, 8), ring(5, 17)]
    upper += [ring(5*math.cos(i*math.pi/64), 17+5*math.sin(i*math.pi/64)) for i in range(1, 33)]
    write_mesh("ArenaBanks", lower)
    write_mesh("ArenaShell", upper, ceiling=True)
    scene_path = ASSETS / "Scenes/Main.plutoscene"
    blocks = re.split(r"(?=^ENTITY\t)", scene_path.read_text(), flags=re.M)
    retained = []
    for block in blocks[1:]:
        fields = block.splitlines()[0].split("\t")
        index = int(fields[1])
        if index < 20 or (index >= 100 and fields[2] == "0") or 61 <= index <= 64:
            continue
        if index in (20, 22):
            fields[5] = f"{'-' if index == 20 else ''}22.000000,0.750000,0.000000"
            block = "\t".join(fields) + "\n" + block.split("\n", 1)[1]
        block = re.sub(r"(PROPERTY\tgoalLine\t0\t)[^\t]+", r"\g<1>41.000000", block)
        block = re.sub(r"(PROPERTY\tgoalHalfWidth\t0\t)[^\t]+", r"\g<1>8.000000", block)
        retained.append(block)
    geometry = entity(1, "Arena Floor", (0, -0.25, 0), (70, 0.5, 42), "Arena")
    geometry += entity(2, "Concave Lower Banks", (0,0,0), (1,1,1), "Wall", "project://Models/ArenaBanks.glb")
    geometry += entity(3, "Translucent Walls and Ceiling", (0,0,0), (1,1,1), "StadiumGlass", "project://Models/ArenaShell.glb")
    geometry += entity(16, "Centre Line", (0,0.018,0), (0.18,0.035,41), "Ball", collider=False)
    index = 100
    for sign, team in [(-1, "Blue"), (1, "Orange")]:
        for name, pos, size, mat in [
            ("Goal Floor", (sign*40.5,-0.25,0), (11,0.5,16), "Arena"),
            ("Goal Back", (sign*46.25,4,0), (0.5,8,16.5), "StadiumGlass"),
            ("Goal North", (sign*40.5,4,-8.25), (11.5,8,0.5), "StadiumGlass"),
            ("Goal South", (sign*40.5,4,8.25), (11.5,8,0.5), "StadiumGlass"),
            ("Goal Roof", (sign*43,8.25,0), (6.5,0.5,16.5), "StadiumGlass"),
            ("Goal Crossbar", (sign*40,8.15,0), (0.35,0.3,16.5), "Goal"+team),
            ("Goal Post North", (sign*40,4,-8.15), (0.35,8,0.3), "Goal"+team),
            ("Goal Post South", (sign*40,4,8.15), (0.35,8,0.3), "Goal"+team),
        ]:
            geometry += entity(index, team+" "+name, pos, size, mat)
            index += 1
    for x, z in [(-26,-14),(-26,14),(-12,-12),(-12,12),(0,-17),(0,17),(12,-12),(12,12),(26,-14),(26,14),(0,0)]:
        geometry += entity(index, f"Boost Pad {index}", (x,0.08,z), (2.4,0.12,2.4), "BoostPad", collider=False, script="BoostPad")
        index += 1
    scene_path.write_text("SCENE\t1\n" + geometry + "".join(retained))
    project = ROOT / "RocketLeg.plutoproject"
    lines = project.read_text().splitlines()
    additions = {"Models/ArenaBanks.glb": "Mesh", "Models/ArenaShell.glb": "Mesh",
                 "Materials/StadiumGlass.plutomaterial": "Material",
                 "Materials/BoostPad.plutomaterial": "Material", "Scripts/BoostPad.cs": "Script"}
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
    for relative, kind in additions.items():
        if relative not in known:
            lines.append(f"ASSET\tproject://{relative}\t{(ASSETS / relative).stat().st_size}\t{kind}")
    project.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
