# Terrain Holes

A heightfield cannot overhang, so a cave, a mine shaft or a basement under a terrain is built from two parts. Meshes
make the space (the rock of a cave, a shaft's lining). **Holes** cut the terrain cells that would stand in that space.
A hole cell is not drawn. It does not collide, and it has no surface for queries, foliage or placement. Everything
else about the terrain is unchanged.

Holes are whole cells. Cell `(x, z)` is the quad between samples `(x, z)` and `(x + 1, z + 1)`, so a terrain of
`W x D` samples has `(W - 1) x (D - 1)` cells, each **Cell Size** wide. A hole's edge therefore follows the cell
grid. Hide it behind the geometry that fills the hole (a cave's rock, a shaft's rim), at least a cell's diagonal past
the space you cut.

## Editing holes

Select the terrain, enable painting, and choose **Paint > Mode: Hole**. Then drag over the terrain: every cell whose
centre is under the brush is cut, and a brush smaller than a cell cuts the cell under it. **Mode: Fill Hole**
restores cells. The toolbar shows how many cells are cut. Strokes are ordinary scene edits, so undo works. Holes are
saved with the scene and kept when the terrain is resized (cells outside the new size are dropped).

## What a hole changes

| System | Behaviour |
| --- | --- |
| Rendering, shadows, GI, baking | The cell's triangles are left out at every LOD. A coarse LOD quad that covers a hole is replaced by the full-detail quads of its solid cells. Those geomorph onto the coarse surface like the rest of the LOD, so no cracks open. |
| Physics (Bullet) | A terrain with holes collides as a triangle mesh of its solid cells, split along the same diagonal as the heightfield and the render mesh. A terrain without holes keeps the heightfield shape. Changing holes rebuilds the collider at once, also for queries made later in the same frame. |
| `TrySampleSurface`, `TrySampleWorldSurface`, managed `TerrainComponent.TryGetSurface` | Return false over a hole, so foliage brushes, surface placement and scripts find no ground there. |
| `TerrainComponent::Raycast` (editor picking, brushes) | Passes through holes and can hit the terrain beyond. |
| `GetHeightAtLocalPosition` | Unchanged: the interpolated height. It is a sampler, not a surface test. |

## API

```cpp
#include "PlutoGE/scene/TerrainHoleMask.h"

terrain->SetHole(cellX, cellZ, true);             // one cell; returns whether it changed
terrain->IsHoleAtWorldPosition(worldPosition);    // or IsHoleAtLocalPosition(x, z)

scene::TerrainHoleMask holes(width - 1, depth - 1);
holes.PaintCircle(centreX, centreZ, radius, true); // sample coordinates: local position / cell size
terrain->SetHoles(std::move(holes));               // replaces the mask
```

`TerrainHoleMask` is plain data with no scene or renderer dependency. Tools and procedural generators can build one
and hand it to a terrain. `GetHolesRevision()` changes with every edit.

## Scene format

The `TerrainComponent` property `HoleMask` (string) holds the cut cells as run lengths:
`<cellsX>x<cellsZ>:<solid>,<cut>,<solid>,...` in row order (z, then x), starting with a solid run (which may be 0).
An empty value means no holes, and terrains saved before holes existed load without any. Generators can write the
property directly. For example, SurvivalGame's `Tools/levelkit/landscape.py` has `encode_holes`, and its
`levelkit.caves` builds a cave's rock and the holes it needs.

A property that is missing when a snapshot is deserialized keeps the current holes, as `HeightSamples` keeps the
heights. So the engine always writes `HoleMask`, even when it is empty.

## Limits

- Hole edges are stepped at the cell size. Finer edges need a denser terrain, or geometry that covers the edge.
- Lighting inside a cut space comes from what encloses it. Use an Ambient Light Volume
  ([ambient-probe-grids.md](ambient-probe-grids.md)) so the sky does not light a cave. Volumetric fog's ambient term
  is not occluded by ambient volumes yet, so fog inside an enclosed space stays lit by the sky.

Tests: `PlutoGETerrainHoleTests` (mask editing and encoding, persistence, surface queries, CPU raycast, every LOD's
triangles, Bullet collision).
