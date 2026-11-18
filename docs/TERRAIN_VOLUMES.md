# Terrain volumes

A heightfield has one height per point, so it cannot hold a cave, an overhang or a pit dug sideways. A **Terrain
Volume** adds 3D ground to a terrain where something needs it. The terrain stays the ground everywhere else. Where
edits move the surface, the volume cuts the terrain's cells and draws that ground itself, with collision.

The design and its later phases (rendering, a script API, procedural caves, an editor brush) are in
[the terrain volumes plan](dev/TERRAIN_VOLUMES_PLAN.md). For hand-modelled, indestructible caves, use a mesh and
[terrain holes](TERRAIN_HOLES.md) instead.

## Adding a volume

Add **Rendering > Terrain Volume** to an entity with a Terrain component. For a terrain world, add it to the world's
**Tile Template**: every tile then has one, and each tile keeps its own volume file.

| Setting | Meaning |
| --- | --- |
| Voxel Size | Metres between density samples (default 0.5). Smaller digs are finer and cost more. |
| Chunk Voxels | Samples per chunk side (default 32). A chunk is meshed, drawn and collided with as one entity. |
| Material Asset | What chunk meshes draw with. Empty uses the terrain's material (a triplanar terrain material comes with phase 4). |
| Volume File | Where the dug ground is saved. A world sets its tiles' (`tile_x_z_volume.plutovolume` beside the heights). |
| Collision | Chunks get mesh colliders (on by default). |
| Meshes Per Frame | Chunks meshed and applied each frame. |
| Breach Tolerance | Voxels the surface must move before the terrain's cells are cut there (0.25). |
| Surface Tolerance | Voxels within which volume ground counts as the terrain's own and is left to it (0.5). |
| Overlap Cells | Uncut cells around a cut where the volume still draws the surface, hiding seams (1). |
| Overlap Sink | Metres that overlap sinks below the terrain, so the terrain wins where both draw (0.05). |

Changing the voxel size, chunk size or volume file starts the volume again from its file: save first.

## Editing from code

Edits are signed-distance shapes applied in world space:

```cpp
#include "PlutoGE/scene/components/TerrainVolumeComponent.h"

auto *volume = terrainEntity->GetComponent<scene::TerrainVolumeComponent>();
using namespace scene::volume;
volume->Apply(SphereShape(hitPoint, 1.5f), {VolumeEditMode::Subtract});                // dig
volume->Apply(CapsuleShape(start, end, 2.0f), {VolumeEditMode::Subtract});             // a tunnel
volume->Apply(BoxShape(centre, {2.0f, 1.0f, 2.0f}), {VolumeEditMode::Add, 3});        // build up, terrain layer 3
volume->Apply(SphereShape(centre, 3.0f), {VolumeEditMode::Paint, 2});                 // repaint rock, no shape change
volume->SaveFile();                                                                    // keep it
```

Results arrive over the next few frames. Meshing runs on worker threads, `Meshes Per Frame` chunks at a time. The
terrain's cells open only once the meshes that fill them are in, so no frame shows an empty hole. `Flush()` finishes
everything at once (loading screens, tools, tests), and `IsUpToDate()` says whether anything is still outstanding. A
C# API is planned (phase 5).

## What changes in the scene

- **Chunk entities**: transient children of the terrain entity, named `Volume Chunk x,y,z`, tagged
  `TerrainVolumeChunk` and with the terrain entity's own tags (so `ground` checks keep working). Each has a mesh and,
  with Collision on, a mesh collider. They are not saved; the volume file is. Removed by an undo, they come back.
- **Derived holes**: the cells the volume took over are cut through `TerrainComponent::SetDerivedHoles` under the
  source `TerrainVolume`. The terrain draws, collides and answers surface queries with its authored holes and derived
  ones together (`GetEffectiveHoles`). The hole brush, `GetHoles` and the saved `HoleMask` see only the authored holes.
- **Caves under the ground** cut nothing: the terrain still draws the surface above, and the volume draws the cave.
- **Navigation** meshes re-sample the chunks a change touched.
- Disabling the component gives the terrain its ground back; enabling it digs again. Removing it removes its ground.

## Limits

- Chunk meshes have no proper material yet (phase 4).
- Foliage and detail grass still grow over the old surface of cut cells (phase 5).
- Sculpting a terrain later moves its unedited ground; dug chunks keep the shape they had.
- Cave mouths close up in the distance, where the world's overview draws instead of the tile.
