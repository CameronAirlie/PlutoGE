# Large worlds: plan

Written 2026-11-15. Six engine features that together let a project build a multi-kilometre, multi-biome world
(the SurvivalGame island). Each feature is a module with a pure, engine-free core that tests can drive directly, and a
thin component/editor layer over it. Status per feature is at the end.

## Principles

- **Pure cores, thin components.** Formats, evaluators, grids and policies live in plain classes and free functions
  under `engine/scene` with no renderer, editor or `Engine` singleton dependency. File access is injected (as
  `TerrainLayerStack::Services` already does). Components translate between those cores and the scene.
- **Data lives where the engine already looks for it.** Heights stay `TerrainComponent` samples, procedural texturing
  bakes into `TerrainWeightMap`, scatter writes ordinary `FoliageComponent` instances, lakes are `OceanComponent`
  areas, rivers are `SplineComponent` ribbons. Rendering, collision, GI and grass need no new paths.
- **Open for extension.** Rule inputs, height file encodings, tile content and water bodies are enumerations or
  interfaces with one place to add a case, and serialized formats carry versions.
- **Old scenes load unchanged.** Every new property is optional; missing ones keep today's behaviour.

## 1. Heightmap import (`scene/TerrainHeightImport.h`)

`DecodeTerrainHeightField(bytes, format, options)` turns a file into a normalized `TerrainHeightField` (0..1 samples,
row 0 at the terrain's near edge). Formats: 8- and 16-bit images (PNG/TGA/BMP/JPG via stb, 16-bit PNG kept at 16 bits),
raw R16 (little or big endian) and raw R32F (`.r16`, `.raw`, `.r32`), and `.plutoheight`. Raw sizes are given or
inferred as square. `ResampleHeightField` resizes bilinearly (to fit 2^n+1 tiles). `TerrainComponent::ImportHeightMap`
applies it with a height range; `LoadHeightMap` keeps its signature and now reads 16-bit sources at full precision.

## 2. Binary height files (`scene/TerrainHeightData.h`)

`.plutoheight` v1: a 32-byte little-endian header (magic `PLHT`, version, width, depth, encoding, flags, height range)
followed by the samples as float32 or unorm16, then an optional one-sample border ring (neighbouring tiles' samples,
used for seamless normals). `TerrainComponent` gains `HeightData` (asset reference). With it set, the heights load
from and save to that file and the scene no longer carries `HeightSamples` (the heavy text property), exactly as layer
weights already live in PNGs. Sculpting records undo as height regions (like `TerrainLayerStroke`), and the editor
assigns a file on the first stroke and offers *Store Heights In File* for existing terrains.

## 3. Procedural biomes (`scene/procedural/*`, `ProceduralTerrainComponent`)

A deterministic CPU evaluator, a subset of `PROCEDURAL_TERRAIN_PLAN.md` phases 1-4:

- `ProceduralNoise`: integer-hash value noise, fbm and ridged, identical on every platform.
- `TerrainAttributes`: height, slope and curvature sampled from any height source in world space.
- `TerrainRule` / `TerrainRuleStack`: inputs `Constant | Height | Slope | Curvature | Noise | Mask | SplineDistance`,
  remap band with falloff, invert, combine (`Multiply | Add | Subtract | Min | Max | Replace`) with opacity.
- `ProceduralMaskSet`: named masks from images (any channel), placed over a world rectangle, so masks painted or
  exported (Gaea flow/biome maps) for the whole world line up across tiles.
- `ProceduralTerrainDefinition`: biomes. A biome has a weight (a mask and/or rule stack), texture rules (terrain layer
  name + coverage stack) and scatter rules (foliage type name, spacing, density stack, slope/height limits, scale,
  alignment, footprint). Encoded as versioned text in one property, ready to move into a `.plutobiome` asset.
- `ProceduralTexturing::BakeWeights`: biome weights normalized per texel; each biome's layers use coverage semantics
  (bottom to top, each layer takes its coverage of what remains); results blend by biome weight into a
  `TerrainWeightMap`. Detail grass already reads its density from terrain layer weights, so biomes drive grass too.
- `ProceduralScatter::Scatter`: jittered grid at the type's spacing, density x biome weight against a hashed roll,
  limits, footprint exclusion against higher-priority instances, stable IDs with bit 63 set (never colliding with
  hand-placed IDs). Regeneration replaces only procedural instances.
- Evaluation is tile-parallel and pure; `ProceduralTerrainComponent` gathers inputs (heights, masks, spline
  polylines), runs it, and applies the results to the sibling terrain and foliage. `Generate On Load` makes streamed
  tiles generate themselves, so a tiled world stores only heights, masks and one definition.

## 4. Water (`scene/WaterBodies.h`, `RiverComponent`)

- **Lakes** are `OceanComponent`s with an area polygon and calm settings. `TraceWaterOutline` floods the terrain below
  a water level from a seed point and returns the shore as a simplified polygon (at most the ocean's 32 points);
  `OceanComponent::ApplyLakePreset` gives still water. The inspector gets *Fit Area To Terrain*.
- **Rivers**: `RiverComponent` next to a `SplineComponent`. It conforms the spline to the terrain (points on the
  ground, never flowing uphill), configures the spline as a thin water ribbon without collision, and can carve the
  river bed into the terrain (`CarveTerrainAlongPath`, an undoable height edit). Rivers are also `SplineDistance`
  sources for procedural rules (clear trees, paint mud).
- **Queries**: `WaterQueries::SampleWaterSurface(scene, position)` reports the water surface (oceans with their area
  masks, rivers) for gameplay (drinking, swimming, wetness).

## 5. Terrain tiles and streaming (`scene/TerrainWorldGrid.h`, `TerrainWorldComponent`)

- `TerrainWorldGrid`: tiles X x Z, tile resolution (2^n + 1 samples, shared edges), cell size, height scale, origin.
  Tile bounds, the tile under a point, and `SplitHeightField` (a world field to per-tile `TerrainHeightData` with
  border rings).
- `TerrainTileStreamingPolicy`: given loaded/pending tiles and the camera, which tiles to request and release
  (load radius, a larger unload radius for hysteresis, nearest first, an in-flight cap).
- `TerrainWorldComponent`: streams tiles as transient child entities. A tile is built from an optional template prefab
  (any components: terrain settings and layers, collider, detail grass, procedural terrain) or a default terrain with
  collider, then given its height file. File reads and decoding run on worker threads; at most one tile activates per
  frame. Navigation meshes are invalidated over a tile's bounds when it appears or goes. The editor loads tiles around
  the editor camera the same way, and *Import World Heightmap* splits one large heightmap into tile files.

## 6. Distance (`TerrainWorldComponent` overview, terrain LOD)

- **Far terrain**: the world keeps a low-resolution overview of the whole heightmap (written by the import) and draws
  it, tile by tile, wherever detailed tiles are not loaded: one coarse mesh per tile with a skirt hiding the seam,
  coloured by an optional world colour map. A tile's overview hides as soon as the detailed tile is ready.
- **Terrain LOD reach**: `TerrainComponent` gains `LodDistanceScale`, so large cells and tiles can hold detail further.
- Existing systems already cover the rest and are documented in the guide rather than rebuilt: camera-centred DDGI
  cascades, VSM clipmaps, foliage impostors and `foliageDetail`, and reverse-Z depth (far planes of kilometres keep
  precision).

## Verification

`PlutoGETerrainHeightDataTests` (formats, import, resample, split, component persistence and undo regions),
`PlutoGEProceduralTerrainTests` (noise determinism, rules, coverage, biome blending, scatter determinism and
exclusion, definition round trip), `PlutoGEWaterBodyTests` (outline tracing, carving, river conforming, queries),
`PlutoGETerrainWorldTests` (grid, split seams, streaming policy, tile activation with real files, overview). Existing
terrain, layer, hole, foliage, detail grass and scene tests must stay green.

## Status

See the end of `docs/TERRAIN_WORLDS.md` for what was built and what remains.
