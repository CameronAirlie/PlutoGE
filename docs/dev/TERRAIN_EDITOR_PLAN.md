# Terrain editing: one tool for terrains and terrain worlds

Written 2026-11-15. Follows [LARGE_WORLD_PLAN.md](LARGE_WORLD_PLAN.md).

## What was confusing

- **Three components, three UIs.** Terrain (a hard-coded block in `InspectorPanel`), Terrain World (generic properties
  plus a builder) and Procedural Terrain (its own inspector), with no place that says how they fit together.
- **Brushes were scene data.** Paint Enabled, mode, radius, strength, layer, opacity and hardness were saved on the
  terrain component (and became prefab overrides), and the brush lived behind the viewport's Paint popup, reachable
  only after ticking Paint Enabled in the inspector.
- **Worlds needed a prefab** to give tiles their layers, biomes, collider or grass, and editing a tile edited a
  transient copy that was thrown away.
- **Sculpting a world edited one tile**, opening seams at tile edges.
- **Creating** a terrain made a bare heightfield; collision, foliage, grass and biomes were separate steps.

## Design

The scene keeps small, single-purpose components; the editor presents them as one thing.

```
engine (scene)                                   editor (ui)
------------------------------------------       ---------------------------------------------------
TerrainBrush.h        brush structs (pure)        TerrainEditTool       session state: mode, brushes,
TerrainEditSurface    "what a brush edits": one                         target (like FoliageEditTool)
                      terrain or a world's tiles  TerrainEditTarget     selection -> terrain or world
TerrainWorldComponent tile sources (template                            (a tile or the template selects
                      child, prefab, default),                           its world)
                      RefreshTiles, SyncTileEdges, TerrainViewportTool   brush strokes over the target,
                      per-tile weight files                              one undo step each, overlay bar
TerrainComponent      brushes passed in, not      TerrainEditorPanel    the hub: Create, Sculpt, Paint,
                      saved                                              Biomes, Heights, World tabs
                                                  Terrain / World /     compact summaries with
                                                  Procedural inspectors "Open Terrain Editor"
                                                  Hierarchy presets     Terrain, Terrain World
```

- **Brushes leave the scene.** `TerrainSculptBrush`, `TerrainLayerBrush` (exists) and `TerrainHoleBrush` are passed to
  the edit calls. The old properties are still read (and retired), so old scenes load; the legacy setters stay for
  scripts and tests.
- **`TerrainEditSurface`** gathers the terrains a brush acts on and applies a world-space brush to every one it
  overlaps, returning what changed per terrain. A single terrain and a world are the same to the tools.
- **World tiles come from a source** (`TerrainTileSource`): the world's **Tile Template** (an inactive, saved child
  entity: terrain settings and layers, collider, foliage types, grass, biomes; created with the world), else a tile
  prefab, else a default terrain. Editing the template and pressing nothing else: the editor calls `RefreshTiles`,
  which copies the template's settings onto the loaded tiles (heights, weights and holes stay) and regenerates biomes.
- **Seams**: after a world stroke, `SyncTileEdges` makes the shared edges and border rings of the touched tiles and
  their neighbours agree (loaded tiles in memory, unloaded ones in their files).
- **Per-tile weights**: a tile's layer weights live beside its height file (`tile_x_z_weights0.png`); painted tiles keep
  them, and biomes then only plant.
- **The Terrain Editor panel** follows the selection. With nothing terrain-like selected it offers Create (a terrain
  or a world, by size, with collision/foliage/grass/biomes ticked). Otherwise its tabs act on the target: Sculpt and
  Paint (choose the tool here or in the viewport bar), Biomes (the definition editor, moved out of the Procedural
  inspector into a reusable function), Heights (import, height files, LOD, skirts), World (grid, streaming, building
  from a heightmap). Missing pieces (no biomes, no foliage) are one button away.

## Verification

Engine: `PlutoGETerrainEditingTests` (brush structs, retired properties, edit surfaces across tiles, template tiles,
refresh, edge sync with loaded and unloaded neighbours, per-tile weights). Editor: `PlutoGETerrainEditorTests`
(target resolution for a terrain, a world, its tiles and template; tool state). Existing terrain, procedural, world and
foliage suites stay green.
