# Terrain worlds

How to build a world several kilometres across: heightmaps from terrain tools, heights kept in their own files,
biomes that paint and plant the ground, lakes and rivers, terrain streamed in tiles, and what draws in the distance.
The design and its reasoning are in [dev/LARGE_WORLD_PLAN.md](dev/LARGE_WORLD_PLAN.md).

| Feature | Where |
| --- | --- |
| Heightmap import (8/16-bit images, raw R16/R32F, `.plutoheight`) | `scene/TerrainHeightData.h`, Terrain inspector > Heights > Import Heightmap |
| Heights in a `.plutoheight` file | `TerrainComponent::HeightData`, Terrain inspector > Store Heights In File |
| Biomes: procedural layer weights and foliage | `scene/procedural/*`, `ProceduralTerrainComponent` |
| Lakes and rivers, water queries | `scene/WaterBodies.h`, `OceanComponent::ApplyLakePreset`, `RiverComponent` |
| Tiles and streaming | `scene/TerrainWorldGrid.h`, `TerrainWorldComponent` |
| Distant terrain | the world's overview, `TerrainComponent` skirts and `LodDistanceScale` |

## The Terrain Editor

**Window > Terrain Editor** is the one place to make and edit terrain, laid out like the Post Process Editor: a toolbar
(what it edits, and the tool the viewport uses: Select, Sculpt, Paint or Holes), the problems it finds (each opens the
section that fixes it), and a list of sections beside the selected one: Sculpt, Paint, Biomes, Grass, Heights, World
(worlds only) and Setup. It follows the selection: a terrain, a terrain world, one of the world's tiles or its tile
template (all of which edit the world).

- **Nothing selected**: *Create Terrain* (a size, a cell size, and ticks for collision, foliage, detail grass and
  biomes) or *Create Terrain World* (tiles, tile samples, cell size; it starts as flat tiles in the project). The
  hierarchy's Create menu has both too.
- **Sculpt**, **Paint**, **Holes**: the terrain tool. Choose it here, in the inspector, or in the viewport's Paint menu;
  a bar at the bottom of the viewport holds the brush. Drag on the ground; Shift swaps raise and lower (and cut and
  fill), `[` `]` resize, Ctrl+click picks the Flatten height, Esc stops. Each stroke is one undo step. On a world, a
  stroke reaches every loaded tile under the brush and the tiles' shared edges and borders are kept matched, so
  sculpting across tile edges leaves no seams. Holes are for single terrains (tile holes are not saved yet).
- **Biomes**: *Add Biomes* (a starter biome: grass, rock on slopes), then the biome rules; *Generate* on a terrain,
  or nothing on a world: its loaded tiles regenerate when the rules change. Under the rules, **Plant Shadows** sets
  whether the plants cast shadows (the Foliage component's default and shadow distance) and lists each plant's own
  choice: plants from a foliage type asset open in the Foliage Editor (*Rendering > Shadows*), scene-only ones are set
  in place.
- **Grass**: the detail grass layers (*Add Detail Grass* first): what each looks like and where it grows (a terrain
  layer's paint, slope, a height band, bare patches). On a world, every tile grows them.
- **Heights**: import a heightmap, the height file, LOD reach. On a world: build its tiles from one heightmap, or flat.
- **World**: the grid (applied together, since a new grid drops every tile), streaming, loaded tiles.
- **Setup**: the material, and the parts (collision, foliage, detail grass, biomes) as ticks.

The brush is the editor's, not the terrain's: nothing about it is saved with the scene. The Terrain, Terrain World and
Procedural Terrain inspectors show a summary and the same tool buttons, and point here.

**Worlds and their Tile Template.** A world's settings live on its **Tile Template**, an inactive child entity created
with the world: the terrain's material and layers, the collider, foliage types, detail grass and biomes. Tiles are
copies of it. Edit it from the Terrain Editor or select it (Setup > *Select Tile Template*) and use the inspector like
any entity: the loaded tiles pick up the change within a moment, keeping their heights and paint (adding or removing a
component rebuilds them). A tile prefab can replace the template. Each tile keeps its heights in its height file and its
paint in `tile_x_z_weights0.png` beside it; a painted tile keeps its paint, and its biomes then only plant it.

## A suggested workflow

1. **Sketch** the layout at low resolution (a painted greyscale of land and sea, and one of biome regions).
2. **Make the heightmap** in a terrain tool (Gaea, World Machine, World Creator) from the sketch: erosion is what makes
   mountains read as real. Export **16-bit PNG or raw R16** at the world's sample count (see the grid below), plus any
   masks (flow, slope, biome regions) as images.
3. **Create the world** in the Terrain Editor, then *Heights > Build From Heightmap*. This writes the tiles and an
   overview into the project.
4. **Set up the tiles** (Setup and Paint tabs): material layers (grass, rock, snow, sand...), foliage types, detail
   grass, and biomes. Put masks in the biome definition with the **world's rectangle** so every tile reads the same image.
5. **Touch up** with the Sculpt and Paint tools.
6. **Add water**: an Ocean for the sea, lakes (an Ocean per lake, *Apply Lake Preset*, *Fit Area To Terrain*), and
   rivers (a spline with a River component).
7. **Play**: tiles stream around the main camera; in the editor they stream around the editor camera.

## Heightmaps

`DecodeTerrainHeightSource` reads:

| Source | Notes |
| --- | --- |
| PNG, TGA, BMP, JPG | The first channel. **16-bit PNGs keep all 16 bits**; 8-bit images give only 256 steps (terracing on large terrains). |
| Raw 16-bit (`.r16`, `.raw`) | Unsigned, little endian by default. No header: give the width and depth, or 0 x 0 for a square inferred from the size. |
| Raw 32-bit float (`.r32`, `.f32`) | Heights as stored (not normalized). |
| `.plutoheight` | The engine's own height file (below). |

Images and raw files list their top row first, which is the terrain's far (+Z) edge; *Top Row Is Far Edge* flips them
(on by default). Normalized sources map 0..1 onto a **height range** (default 0..Height Scale). Importing replaces the
grid's size and clears holes when the size changes. *Import Heightmap* in the Terrain inspector is one undo step.
`ResampleHeightField` resizes a field bilinearly, keeping corners on corners.

## Height files

A terrain whose **Height Data** names a `.plutoheight` file loads its heights from it and saves them to it; the scene no
longer carries the heights (the `HeightSamples` text, about 10 bytes a sample). This is how the layer weights already
work: the scene names the file, and the file holds the data.

- *Store Heights In File* moves an existing terrain's heights into `Assets/TerrainHeights/<scene>_<entity>.plutoheight`.
  Sculpting does this by itself on the first stroke when the editor has a project.
- Sculpt strokes record undo steps holding just the samples they changed, and write the file when the stroke ends.
- The file is read only when the scene names a different file than the one loaded, so undoing an unrelated edit never
  throws away newer sculpting.
- Undoing *Store Heights In File* puts the samples back into the scene.

Format (version 1, little endian): a 32-byte header (`PLHT`, version, width, depth, encoding, flags, height range),
the samples as float32 (exact) or unorm16 (half the size, quantized to 1/65535 of the range), then an optional
**border ring**: the two samples beyond each edge that the neighbouring tiles hold, so tiles smooth their surface and
compute normals identically across a seam. Files larger than 16385 a side are refused.

## Biomes (Procedural Terrain)

A **Procedural Terrain** component on a terrain's entity generates the terrain's layer weights and fills the Foliage
component beside it. Results go where the engine already reads them, so rendering, collision, detail grass (which takes
its density from layer weights) and GI need nothing new. Hand-placed foliage is never touched: generated instances have
IDs with bit 63 set, derived from where they grow, so regenerating replaces exactly the generated ones.

**Rules.** A rule reads an input, remaps it to 0..1 and combines it into a running value that starts at 1:

| Input | Value |
| --- | --- |
| Constant | The rule's value |
| Height | World height of the surface (m) |
| Slope | Degrees from horizontal |
| Curvature | Laplacian of the surface: positive in hollows and valleys, negative on ridges |
| Noise | Value, fBm or ridged noise in world space (scale in metres, octaves, seed) |
| Mask | A named mask image |
| Distance | Distance to splines tagged (or named) the rule's source, from their edge (rivers, roads, trails) |

*Band* remaps give 1 between Min and Max, easing to 0 over Falloff; *Linear* maps Min..Max to 0..1 (for noise and
masks). Blends: Multiply (a filter), Add, Subtract, Min, Max, Replace, each with an opacity. An empty stack is 1.

**Biomes.** Each has a weight (a mask and/or rules), layer rules and scatter rules. Biome weights are normalized where
they overlap and blurred over *Biome Blend* metres (evaluated past the terrain's edge, so tiles blend alike across
seams). Layers are bottom to top: each later layer takes its coverage of what the ones above it left, and the first
takes the rest. Where no biome paints, the terrain's first layer shows.

**Scatter.** Candidates sit on a jittered grid at the rule's spacing, **aligned to the world**, so neighbouring tiles
continue each other's pattern and an instance belongs to exactly one tile. A candidate grows when a hashed roll is
below biome weight x density x density rules, within the slope and height limits, and clear of the footprints of
instances already placed (larger plants are placed first). Scale, yaw, slope alignment and sinking are per rule.

**Masks** are images placed over a world rectangle; a zero size spans the terrain being generated. Masks are decoded
once and shared by every tile.

**Generating.** *Generate* in the inspector is one undo step (layer weights saved to their files, foliage in the
scene). **Generate On Load** makes the component generate by itself when it first updates; streamed tiles use it
through the world, which runs generation on a worker thread. Generation is deterministic: the same definition and
heights give the same result on every machine.

The definition is stored as versioned text in one property (`ProceduralTerrainDefinition::Encode`), ready to move into
its own asset.

## Water

**Lakes** are Ocean components with an area: *Apply Lake Preset* (still water, short visibility, Invert Area Mask on, so
only the area is water), then place the entity at the water level over a basin and *Fit Area To Terrain*: it floods the
basin from the entity's position and makes the shore the ocean's area (at most 32 points, reaching a little under the
banks). Lakes spanning several tiles use one tile's heights for the fit.

**Rivers**: a Spline Track with a **River** component.
- *Make Water Ribbon*: the spline becomes open, thin and without collision, drawn with a translucent water material
  unless the spline names a material asset of its own (use a shader graph with scrolling normals for flowing water).
- *Conform To Terrain*: points go onto the ground minus *Surface Drop*, never flowing uphill (source = first point).
- *Carve River Bed*: lowers every terrain the river crosses into a rounded channel (*Depth*) and banks (*Bank Width*),
  as one undo step (saved to height files).
- Tag the entity (say `River`) and use **Distance** rules to clear trees and paint mud along it.

**Queries**: `SampleWaterSurface(scene, position)` gives the highest water surface over a point (oceans with their area
masks and waves, rivers with their flow velocity); `IsUnderwater(scene, position)`.

## Terrain worlds (tiles and streaming)

A **Terrain World** component describes a grid of tiles. Tiles share their edge samples, so a world of N tiles across
has `N x (Tile Samples - 1) + 1` samples; use 2^n + 1 tile samples (129, 257, 513).

| Grid | Example |
| --- | --- |
| 8 x 8 tiles of 257 samples at 1 m | 2 km x 2 km, a 2049 x 2049 heightmap |
| 8 x 8 tiles of 257 samples at 2 m | 4 km x 4 km, the same heightmap |
| 16 x 16 tiles of 257 samples at 2 m | 8 km x 8 km, a 4097 x 4097 heightmap |

**Streaming.** Tiles within the load radius of the viewer load (nearest first, a few file reads in flight), and go once
beyond the unload radius. Reading and decoding run on worker threads; *Activations Per Frame* tiles become terrains each
frame (each builds its mesh and collision). The viewer is the editor camera while the game is not running (turn off
*Stream In Editor* to stop that), else the scene's main camera; gameplay and tools can set an override.
`LoadAround(position)` streams synchronously (loading screens, tests).

**Tiles** are transient child entities, never saved, made by a tile source (`TerrainTileSource.h`): the world's Tile
Template (above), else a **Tile Prefab**, else a plain terrain with the Tile Material and a terrain collider. Each tile
keeps its height file, so sculpting a tile in the editor saves into it; `SyncTileEdges` keeps neighbours' shared
edges and border rings in step (loaded tiles in memory, unloaded ones in their files).
Navigation meshes are invalidated over a tile when it appears or goes.

**Distance.** The world's **overview** (written by *Build World From Heightmap*, *Overview Detail* cells per tile) draws
as one coarse terrain per tile wherever the detailed tile is not loaded, with a deep skirt hiding the seam; it hides
when the tile is ready. Give it a plain far-terrain material with *Overview Material*. Detailed tiles have a smaller
skirt (*Tile Skirt Depth*) against cracks where neighbours sit at different LODs. `SampleHeight` answers from loaded
tiles, else the overview.

**What already scales.** DDGI is camera-centred cascades, shadows are virtual shadow map clipmaps, foliage hands over
to impostors with distance (`foliageDetail` per quality tier), and depth is reverse-Z, so far planes of kilometres keep
their precision. Raise the game camera's far plane (and the editor camera's) to see the horizon. Terrains have a
**LOD Distance Scale** (raise it to hold ridgelines' shapes further away) and layer **Detail Distance** (the macro
colour beyond it).

## Limits and next steps

- Holes cannot be cut in a world's tiles yet (the tile files hold heights, not holes).
- Undoing paint on a tile that has since unloaded leaves that tile's weight file as it is.
- Tiles are axis-aligned and unscaled relative to the world entity; terrains are assumed unrotated by carving.
- Procedural texturing and scatter regenerate a whole terrain (no dirty regions yet); scatter runs on one thread.
- Biomes paint up to the terrain's 8 material layers.
- The overview uses one material; a world colour map for it is not implemented.
- `.plutoheight` files are listed as plain files in the Content Browser (no dedicated asset type yet).
- Managed (C#) bindings for the new components and water queries are not written yet.
