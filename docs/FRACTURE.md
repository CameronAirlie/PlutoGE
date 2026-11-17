# Fracture

`Fracture.Shatter` breaks the meshes under an entity into physics debris: a rock that bursts when mined, a crate
that splinters, a statue knocked to pieces. Each piece keeps the source's surface, materials and position, is
closed by flat interior faces, and is spawned as an ordinary entity with a convex hull collider and a dynamic
rigidbody already flying apart. The source is not touched; hide or destroy it yourself.

## Using it from C#

```csharp
using System.Numerics;
using PlutoGE.ScriptCore;

public sealed class MineableRock : ScriptBehaviour
{
    [SerializedField] public int Hits = 3;

    public void Strike(Vector3 point, Vector3 direction)
    {
        if (--Hits > 0)
            return;
        var pieces = Fracture.Shatter(GameObject, new ShatterSettings
        {
            Pieces = 10,
            Seed = (int)GameObject.EntityId,      // same rock, same break, on every machine
            HasImpact = true,
            ImpactPoint = point,
            ImpactDirection = direction,
            Tag = "Debris",
        });
        GameObject.Destroy();
        foreach (var piece in pieces)
            Timers.After(8.0f, () => piece.Destroy());   // no owner: the rock's script is gone
    }
}
```

`Fracture.IsSupported` is false on hosts built before fracture; `Shatter` then returns no pieces and the caller
should fall back (destroy the rock, spawn a prefab).

Pieces are ordinary root entities named `"<source name> piece N"`, at unit scale, placed at each piece's centre of
mass and rotated like the source mesh. Move, scale, tag or destroy them through `GameObject` like anything else;
their `RigidbodyComponent` reports mass and velocity, and their `ColliderComponent.Shape` is
`ColliderShape.ConvexHull`. They are not saved with the scene.

### Settings

`ShatterSettings` is a record struct; every property has a default.

| Setting | Default | Meaning |
| --- | --- | --- |
| `Pieces` | 8 | Pieces for the largest mesh in the subtree. Smaller meshes get fewer, in proportion to the volume of their bounds, at least one. A mesh given one piece is spawned whole. |
| `Seed` | 0 | The same seed breaks the same mesh the same way, on every platform. |
| `HasImpact`, `ImpactPoint` | false, zero | Where the blow landed, in world space. Points off the mesh are moved onto its hull. |
| `ImpactBias` | 0.5 | 0..1. How far the Voronoi seeds are pulled towards the impact: higher gives small chips there and big chunks far away. |
| `ImpactDirection`, `ImpactSpeed` | zero, 1.5 m/s | Velocity added to every piece along the (normalised) direction. Zero direction adds nothing. |
| `BurstSpeed` | 2.5 m/s | Velocity outward from the centre of the bounds of everything that breaks. A little random jitter (15% of burst plus impact speed) is added. |
| `Spin` | 4 rad/s | Each piece spins about a random axis at up to this rate. |
| `Density` | 2600 kg/m³ | Mass is volume times density (rock is 2,500 to 2,800), never under 0.05 kg. |
| `MinPieceSize` | 0.04 m | Pieces whose largest side is shorter are left out, so dust does not become bodies. |
| `Tag` | "" | Given to every piece. |

One shatter spawns at most 1,024 pieces.

## What breaks

Every **active, visible** `MeshComponent` in the root's subtree, root included, with a mesh. Only the submeshes the
component draws are used (a model node drawing one submesh of a shared mesh breaks only that submesh), each placed
as the renderer places it: entity world transform, mesh position and rotation offsets, pivot offset, submesh
offsets and imported node transforms. Non-uniform scale, shear and mirroring are baked into the piece geometry, so
pieces have unit scale and appear exactly where the source was drawn. Skinned meshes break in their bind pose.
Flat meshes (quads, decals) have no inside and are skipped.

Each piece's `MeshComponent` draws runtime geometry with one submesh per material slot used, and shares the
source's materials slot for slot (the same material objects, so tints, inline materials and asset references carry
over, and the piece keeps the source's instance materials alive if the source is destroyed). Shadow casting
follows those materials. Pieces are not static.

## How it works

Native code: `scene::FractureMesh` (`engine/scene/include/PlutoGE/scene/procedural/MeshFracture.h`), pure geometry,
and `scene::Shatter` (`engine/scene/include/PlutoGE/scene/Shatter.h`), which spawns the entities. The managed
bridge is `Fracture.cs`, `Native/ScriptBridge.Fracture.cs` and `engine/scripting/src/FractureScriptApi.cpp`.

1. **Hull.** The convex hull of the mesh's vertices (Bullet's `btConvexHullComputer`, positions deduplicated).
2. **Seeds.** `Pieces` points uniformly inside the hull from a splitmix64 generator (the standard library's
   distributions differ between implementations), each then pulled towards the impact by
   `ImpactBias × (0.35..0.95)`.
3. **Cells.** For every seed, its Voronoi region: the hull's bounding box cut by the bisector planes with the other
   seeds, nearest first, stopping once a bisector lies beyond the region's farthest corner. The cell's solid is
   that region ∩ hull: hull faces clipped to the region's walls, plus each wall's section through the hull clipped
   to the other walls. Its volume and centre of mass come from these faces.
4. **Surface.** A source triangle whose three corners are nearest the same seed lies wholly in that cell (cells are
   convex) and is copied with its shared vertices. The others are clipped against the walls of each cell they
   touch, interpolating every vertex attribute (position, normal, tangent, both UV sets, skin weights) and keeping
   the triangle's material slot. Degenerate triangles are dropped; nothing is welded.
5. **Caps.** Each wall face becomes the piece's interior: flat normals, UVs projected on the wall's plane at one
   repeat per metre (`FractureSettings::capUvScale` natively), tangents from the same frame. **Caps draw with
   material slot 0**, the first material the source draws; a separate interior material is a future option.
6. **Pieces.** Positions are re-centred on the cell's centre of mass, which becomes the piece entity's origin and
   so the rigidbody's centre of mass. Empty pieces and those under `MinPieceSize` are dropped.

The convex hull collider (`ColliderShape::ConvexHull`) is built from the piece's mesh: a `btConvexHullShape` of its
vertices scaled by the entity's world scale, reduced with `btShapeHull` to a few dozen points when there are more
than 64, with a collision margin of 5% of its thinnest side (1 mm to 1 cm) instead of Bullet's 4 cm, so small debris
does not float. Hull shapes are shared through the scene's shape cache like mesh colliders. CCD sweeps a sphere a
quarter of the hull's thinnest side.

### Limits

- **Caps follow the convex hull, not the surface.** For a convex source (most rocks) pieces are watertight and the
  piece volumes add up to the source's. For a concave source, caps reach across hollows and overhang the surface
  slightly, and piece volumes (and masses) add up to the hull's volume, which is larger than the mesh's. Open,
  non-manifold or self-intersecting meshes never crash or produce NaNs, but their caps can look wrong.
- Cuts are planar Voronoi walls: pieces are convex-ish chunks, not splinters or cracks along a texture.
- Pieces collide as convex hulls of themselves, simplified past 64 points.
- Pieces are runtime-only (not saved), and each is a separate draw and rigidbody: keep `Pieces` modest and clean up
  debris after a while.

## Performance

Release, measured by `PlutoGEMeshFractureTests` and `PlutoGEShatterTests` (fastest of 20 runs, on a machine with
other work running, so real figures can be lower):

| Input | Time |
| --- | --- |
| 1,000-triangle sphere into 10 pieces | 2.4 ms |
| 5,100-triangle sphere into 10 pieces (7,900 triangles out) | 9.5 ms |
| the same mesh as one piece (mostly the convex hull) | 5.3 ms |
| `Shatter` of a scaled, rotated cube with a child mesh into 7 entities | 0.3 ms |

A sphere is the worst case: every vertex lies on the hull, so the hull step (about half the time) sees all of them.
The pieces' bodies are created on the next physics step, a cheap convex shape each.

## Tests

- `PlutoGEMeshFractureTests`: cube and UV sphere pieces are closed (each piece's mesh volume matches its cell),
  add up to the source volume, keep their surfaces (UVs and normals interpolated, total surface area unchanged),
  are deterministic per seed, get smaller near an impact; degenerate input gives no pieces; timings.
- `PlutoGEShatterTests`: a dynamic ConvexHull cube (and a sphere whose hull is simplified) falls and rests on a box;
  Shatter spawns root entities with runtime geometry, the source materials, ConvexHull colliders and dynamic
  bodies, placed inside the source's drawn volume, weighing volume × density, picked up by the next physics step,
  landing on the ground, with the source untouched.
- `PlutoGEFractureScriptingTests`: a C# probe shatters a rock through `Fracture.Shatter` and round-trips
  `ColliderShape.ConvexHull`.
