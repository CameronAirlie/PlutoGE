# Terrain volumes

A heightfield has one height per point, so it cannot hold a cave, an overhang or a pit dug sideways. A **Terrain
Volume** adds 3D ground to a terrain where something needs it. The terrain stays the ground everywhere else. Where
the ground changes, the volume cuts the terrain's cells and draws that ground itself, with collision.

The ground of a volume comes from four places, applied in this order:

1. **The terrain**: its surface, its painted layers, and an optional rock layer deeper down.
2. **Generated caves** from the terrain's biomes (a Procedural Terrain component).
3. **Volume Stamps** placed in the scene: shapes that carve, fill or repaint. They move with their entity.
4. **Edits**: digging from the editor's Dig tool or from gameplay. Edits are stored and saved.

The design and the phases still to come are in [the terrain volumes plan](dev/TERRAIN_VOLUMES_PLAN.md). For a
hand-modelled cave that never changes, a mesh with [terrain holes](TERRAIN_HOLES.md) also works.

## Adding a volume

Add **Rendering > Terrain Volume** to an entity with a Terrain component. For a terrain world, add it to the world's
**Tile Template**. Every tile then has one, and each tile keeps its own volume file. You can also use the **Add Terrain
Volume** button in the Terrain Editor (**Sculpt > Dig**).

| Setting | Meaning |
| --- | --- |
| Voxel Size | Metres between density samples (default 0.5). Smaller digs are finer and cost more. |
| Chunk Voxels | Samples per chunk side (default 32). A chunk is meshed, drawn and collided with as one entity. |
| Material Asset | What chunk meshes draw with. Empty uses the terrain's layers (see Rendering). |
| Volume File | Where the dug ground is saved. A world sets its tiles' files (`tile_x_z_volume.plutovolume` beside the heights). The Dig tool names one for a lone terrain. |
| Collision | Chunks get mesh colliders (on by default). |
| Meshes Per Frame | Chunks meshed and applied each frame. |
| Rock Layer | The terrain layer that dug ground shows below Rock Depth (-1 keeps the surface's layer all the way down). |
| Rock Depth | Metres below the surface where the rock layer starts (1). |
| Breach Tolerance | Voxels the surface must move before the terrain's cells are cut there (0.25). |
| Surface Tolerance | Voxels within which volume ground counts as the terrain's own and is left to the terrain (0.5). |
| Overlap Cells | Uncut cells around a cut where the volume still draws the surface, which hides seams (1). |
| Overlap Sink | Metres that overlap sinks below the terrain, so the terrain wins where both draw (0.05). |

Changing the voxel size, the chunk size or the volume file starts the volume again from its file. Save first.

## Digging in the editor

Select the terrain or world, then press **Dig** in the viewport's terrain tool bar or in the Terrain Editor's toolbar.

- **Dig** removes ground, **Fill** adds rock of a terrain layer, and **Paint** recolours rock without moving it. Hold
  Shift to swap Dig and Fill.
- **Radius** sets the size of the brush (`[` and `]` in the viewport). **Rate** sets how many dabs a second land while
  the mouse is held. Each dab digs a little deeper, because the cursor follows the dug ground down.
- Each stroke is one undo step. When the stroke ends, every volume it touched saves its file. A world's tiles save
  their own files, even after a tile has streamed out.

## Digging in game (C#)

```csharp
// A shovel: dig where the ray hit, and give the player what came out.
var dug = TerrainVolumes.Dig(hit.Point, 0.8f);
if (dug.RockRemoved > 0)
    inventory.Add(dug.MainLayer == stoneLayer ? "Stone" : "Dirt", (int)MathF.Ceiling(dug.RockRemoved * 4));

TerrainVolumes.Tunnel(start, end, 1.5f);                 // a capsule
TerrainVolumes.Fill(point, 1.0f, layer: 2);              // build rock up, terrain layer 2
TerrainVolumes.Paint(point, 3.0f, layer: 4);             // recolour rock
TerrainVolumes.Edit(TerrainVolumeShape.Box(centre, halfExtents, rotation), TerrainVolumeEditMode.Dig);
```

- Shapes are in world space. They reach every terrain under them, so digging across a world's tile seam needs no
  knowledge of tiles.
- A dig removes foliage instances whose root it carves out.
- `TerrainVolumeEditResult` reports whether the ground changed, roughly how many cubic metres of rock were removed and
  added, the terrain layer most of the removed rock was, and how many foliage instances went.
- Shapes wider than 32 m are refused (`Refused`), so one call cannot stall a frame. Pass `maxExtent` to change the
  limit for that edit.
- The ground changes over the next few frames. `TerrainVolumes.Flush()` lands everything at once.
- `TerrainVolumes.IsSupported` is false on a host from before terrain volumes, and then edits do nothing.

**Save games.** `TerrainVolumes.CaptureEdits()` returns every terrain's edits as bytes, and
`TerrainVolumes.RestoreEdits(bytes)` puts them back. A terrain the bytes do not name goes back to its file.
`TerrainVolumes.ClearEdits()` sends every terrain back to its files, for a new game.

While the game runs, a terrain world keeps the edits of tiles that stream out and gives them back when the tiles return.
Play never writes the tiles' files.

From C++, the same calls are in `PlutoGE/scene/TerrainVolumeEditing.h`: `EditTerrainVolumes`, `FlushTerrainVolumes`,
`CaptureTerrainVolumeEdits` and `RestoreTerrainVolumeEdits`. For one terrain only, use
`TerrainVolumeComponent::Apply`, `ExportEdits`, `ImportEdits` and `ReloadFile`.

## Generated caves

In the Terrain Editor's **Biomes** section, open **Caves** and turn on **Generate Caves**. The terrain needs a Terrain
Volume.

- Tunnels wander through a band of world heights, set by **Altitude**. Where the ground dips into that band, they open
  as cave mouths.
- Each **Cell Size** square of the world starts **Tunnels Per Cell** tunnels of **Tunnel Length**, with **Radius**,
  **Turn Rate** and **Max Slope** shaping them.
- **Chamber Chance** and **Chamber Radius** add chambers, and **Wall Noise** roughens the walls.
- Caves come from the world seed and the cave seed. They are generated in world space, so a world's tiles agree where
  they meet.
- Caves are never saved as edits. Digging into them is saved.

## Volume Stamps

Add **Rendering > Volume Stamp** to any entity, and its shape changes the terrain volumes under it.

- **Shape**: a Sphere (Radius), a Box (HalfExtents), or a Capsule (Radius, plus Length along the entity's Y).
- **Mode**: Carve (a cave or pit), Fill (rock of **Layer**), or Paint.
- **Order** decides overlaps, lowest first. For example, a fill stamp after a carve stamp plugs it.
- Stamps follow their entity's position, rotation and scale. Disabling the component or the entity takes its effect
  away.
- Stamps are saved with the scene, not in the volume file.

## Rendering

Without a Material Asset, chunks draw with the terrain's layers through the terrain shader:

- Each triangle shows one layer (the layer of the rock it was cut from).
- Maps are projected along the three axes, so walls and overhangs do not smear.
- There is no macro albedo in the distance.

Where two layers meet, the edge is hard. Blending is planned (phase 8).

## What changes in the scene

- **Chunk entities**: transient children of the terrain entity, named `Volume Chunk x,y,z`, tagged
  `TerrainVolumeChunk` and with the terrain entity's own tags, so `ground` checks keep working. Each has a mesh and,
  with Collision on, a mesh collider. They are not saved; the volume file is. If an undo removes them, they come back.
- **Derived holes**: the cells the volume took over are cut through `TerrainComponent::SetDerivedHoles`, under the
  source `TerrainVolume`. The terrain draws, collides and answers surface queries with its authored holes and the
  derived ones together (`GetEffectiveHoles`). The hole brush, `GetHoles` and the saved `HoleMask` see only the authored
  holes.
- **Caves under the ground** cut nothing: the terrain still draws the surface above, and the volume draws the cave.
- **Navigation** meshes re-sample the chunks a change touched.
- Disabling the component gives the terrain its ground back, and enabling it digs again. Removing it removes its
  ground.

## Limits

- Foliage removed by digging returns when a procedural tile regenerates.
- Sculpting a terrain later moves its unedited ground; dug chunks keep the shape they had.
- Cave mouths close up in the distance, where the world's overview draws instead of the tile (phase 9).
- Painting the terrain remeshes its volume's chunks shortly after the strokes stop.
