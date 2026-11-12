# Terrain Holes

A heightfield cannot overhang, so a cave, a mine shaft or a basement under a terrain is built from two parts. Meshes
make the space (the rock of a cave, a shaft's lining). **Holes** cut the terrain cells that would stand in that space.
A hole cell is not drawn. It does not collide, and it has no surface for queries, foliage or placement. Everything
else about the terrain is unchanged.

Cell `(x, z)` is the quad between samples `(x, z)` and `(x + 1, z + 1)`, so a terrain of `W x D` samples has
`(W - 1) x (D - 1)` cells, each **Cell Size** wide. A cell is cut whole, or in half: a **half cut** removes the
triangle at one corner and keeps the triangle at the opposite one, so the cell's edge runs diagonally. A rim made of
whole cells steps along the grid; with half cuts at its corners it follows 45 degree lines, which reads as a smooth
curve at a cell size of a metre or two. Hide the rim behind the geometry that fills the hole (a cave's rock, a
shaft's rim), at least a cell's diagonal past the space you cut.

## Editing holes

Select the terrain, enable painting, and choose **Paint > Mode: Hole**. Then drag over the terrain: every cell whose
centre is under the brush is cut, and a brush smaller than a cell cuts the cell under it. **Mode: Fill Hole**
restores cells. The toolbar shows how many cells are cut. Strokes are ordinary scene edits, so undo works. Holes are
saved with the scene and kept when the terrain is resized (cells outside the new size are dropped).

## What a hole changes

| System | Behaviour |
| --- | --- |
| Rendering, shadows, GI, baking | The cut triangles are left out at every LOD. A coarse LOD quad that covers a hole is replaced by the full-detail quads of its solid cells. Those geomorph onto the coarse surface like the rest of the LOD, so no cracks open. |
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

Half cuts:

```cpp
holes.SetCut(cellX, cellZ, scene::TerrainHoleCut::CornerLowXLowZ); // removes the triangle at (x, z)
holes.SmoothEdges();                  // rounds every stepped corner of the cut area
terrain->SmoothHoleEdges();           // the same, on a terrain's own mask
scene::TerrainCellTriangles(cut);     // the triangles a cell keeps (the render mesh and collider use this)
```

`SmoothEdges` looks at whole cells only. An uncut cell with whole cut cells on exactly two adjacent sides (and uncut
cells across from them) loses the triangle at that corner, and a whole cut cell with uncut cells on exactly two
adjacent sides (and cut cells across from them) keeps the triangle at their corner. Straight edges and single-cell
features are left alone.

## Scene format

The `TerrainComponent` property `HoleMask` (string) holds the cut cells as run lengths:
`<cellsX>x<cellsZ>:<solid>,<cut>,<solid>,...` in row order (z, then x), starting with a solid run (which may be 0).
A mask with half cuts uses `<cellsX>x<cellsZ>;<cut>*<run>,<cut>*<run>,...`, where `<cut>` is the `TerrainHoleCut`
value (0 none, 1 whole, 2 to 5 the half cut at corner LowXLowZ, HighXLowZ, LowXHighZ, HighXHighZ). Masks without half
cuts keep the first form, so older scenes and tools are unaffected.
An empty value means no holes, and terrains saved before holes existed load without any. Generators can write the
property directly. For example, SurvivalGame's `Tools/levelkit/landscape.py` has `encode_holes`, and its
`levelkit.caves` builds a cave's rock and the holes it needs.

A property that is missing when a snapshot is deserialized keeps the current holes, as `HeightSamples` keeps the
heights. So the engine always writes `HoleMask`, even when it is empty.

## Limits

- Hole edges follow the cell grid and its diagonals (with half cuts). Curves finer than a cell need a denser terrain,
  or geometry that covers the edge.
- A half-cut cell is split along the diagonal its cut needs, which for two of the four cuts is the other diagonal from
  the heightfield's. On a sloped cell its kept triangle can then sit up to a few centimetres off the interpolated
  height that `GetHeightAtLocalPosition` reports there.
- Lighting inside a cut space comes from what encloses it. Use an Ambient Light Volume
  ([ambient-probe-grids.md](ambient-probe-grids.md)) so the sky does not light a cave. Bake it with sky visibility:
  volumetric fog inside the volume then takes only the share of the sky its probes see, so a cave's fog is dark.

Tests: `PlutoGETerrainHoleTests` (mask editing and encoding, persistence, surface queries, CPU raycast, every LOD's
triangles, Bullet collision).
