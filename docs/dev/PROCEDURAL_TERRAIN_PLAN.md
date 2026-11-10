# Procedural terrain texturing, foliage and biomes: plan

Written 2026-11-10. Status: planned, nothing implemented.

## Decisions

| Question | Decision |
| --- | --- |
| Where results live | **Hybrid.** Rules bake into the data the engine already consumes: terrain weight maps and `FoliageComponent` instances. Collision, proxies, raycasts, GI baking and impostors keep working unchanged. Dense non-colliding ground cover ("detail") is generated on the GPU near the camera from baked density textures and is never stored. |
| Hand edits | **Masks and overrides.** Procedural output can be regenerated at any time. Artists paint include/exclude masks and override layer weights. Hand-placed foliage stays separate. Removing or moving a procedural instance records an override keyed by its deterministic ID. |
| Authoring | **Rule stacks** in the inspector: ordered filters combined with blend operations. No node graph in this milestone, but the evaluator is a flat list of ops, so a graph front end can compile to it later. |
| Scope | Procedural texturing, procedural foliage scatter, biomes, GPU detail grass. |

## What exists and is reused

- `scene::TerrainLayerStack`: up to 8 layers (`render::kMaxTerrainLayers`) and two RGBA8 weight maps saved as PNGs, with height blending, macro albedo and detail distance. Its `Services` injection lets it be tested without an engine. Weights currently come only from `Paint` / `Fill`.
- `scene::TerrainComponent`: the height samples, `TrySampleSurface`, `WorldToTerrainUv`, and layer painting.
- `scene::FoliageComponent` / `FoliageTypeAsset` (`.plutofoliage` v2): instances in cells, stable instance IDs, LODs, impostors, GPU instance culling (`GpuInstanceCulling`, indirect draws), wind (`FoliageWindResponse`), collision cells and proxies. Instances currently come only from the brush.
- `SplineComponent` / `RoadPlacement`: spline inputs for distance rules (roads, rivers and trails clear foliage and paint dirt).
- `TerrainScriptApi` / `FoliageScriptApi`: typed managed bridges to extend.
- The sample generators (`ThirdPersonProject/Tools/levels/verdant.py`, `levelkit/vegetation.py`, `SurvivalGame/Tools/levels/forest.py`) are the working prototype of the rule model: slope, altitude and noise blends, `ease(distance)` masks from trails, rivers and clearings, a jittered grid at a given spacing, cluster noise, `align_to_slope`, `lean`, `sink`, and density scale as a subset. The engine design keeps their behaviour so they can be ported (Phase 6).

Gaps the plan has to close: there is no rule evaluator, no record of where weights and instances came from, no terrain height texture on the GPU, no CPU job system (rule evaluation will use a small tile-parallel loop), and an 8-layer limit that biomes will run into.

## Architecture

```
inputs (per terrain)                     evaluator                    outputs
------------------------------------     ----------------------       -----------------------------------------
heights -> TerrainAttributes             TerrainRuleStack::Eval       procedural layer weights --+
  (slope, aspect, curvature/cavity,      (tile-parallel, per          override weights/coverage -+-> TerrainWeightMap (baked, PNG)
   normal, height)                        texel of the attribute                                       -> renderer, unchanged
painted masks (R8 PNGs)                   grid)                       foliage density per type --> scatter --> FoliageComponent instances
spline / tagged-entity distance fields                                   + exclusion grid             (Origin = Procedural)
biome weights                                                         detail density textures (R8) --> GPU detail renderer
layer weights (for foliage rules)
noise (seeded, integer hash)
```

### New library: `engine/scene/procedural/`

The library is pure CPU, with no renderer or editor dependencies. Engine access comes through injected services, as `TerrainLayerStack::Services` does. Everything in it is unit-testable and deterministic.

- **`TerrainAttributes`**: derived per-texel fields at the weight-map resolution, built from the heights: height (world), slope (degrees), aspect, normal, curvature/cavity (Laplacian at two radii), and later flow accumulation. Rebuilt per dirty rectangle, dilated by the widest kernel.
- **`TerrainRule`**: one filter.
  - Input: `Constant | Height | Slope | Aspect | Curvature | Noise | SplineDistance | TagDistance | Mask | LayerWeight | BiomeWeight`.
  - Noise parameters: type (value, fbm, ridged, cellular), scale, octaves, seed, offset. Noise uses an integer hash so the result is bit-identical on every platform and every run.
  - Remap: `min`, `max`, `falloff` (smoothstep band) plus an optional curve (up to 8 points) and `invert`.
  - Combine: `Multiply | Add | Subtract | Min | Max | Replace` with `opacity`.
- **`TerrainRuleStack`**: an ordered `std::vector<TerrainRule>` that evaluates to a 0..1 scalar field over a rectangle. It compiles to a flat op list (the hook for a future node graph).
- **`DistanceFieldCache`**: distance in world units to splines (sampled polylines) and to entities with a given tag (point or radius). Built once per source revision, using a jump-flood or brute force per tile. Distance rules need these fields over the whole terrain, so a spline edit dirties the band around the old and new spline.

### Procedural texturing

- `TerrainLayer` gains `source` (`Painted` (default, today's behaviour) | `Procedural`) and a `TerrainRuleStack rules`.
- **Layer semantics are coverage, not normalization**, like image layers. Evaluated bottom to top, layer *i* with coverage *c* takes *c* of the remaining weight: `w_i = c * remaining; remaining -= w_i`. The base layer gets the rest. Rules stay intuitive ("rock where slope > 35°") and the weights always sum to 1.
- **Overrides**: two extra RGBA8 planes (painted per-layer weights) plus one R8 coverage plane, in new PNGs next to the weight maps. The final weight is `lerp(procedural, override, coverage)`. In procedural mode the layer brush writes override weights and raises coverage. A new "Restore procedural" brush lowers coverage. Painted-only terrains never create these files.
- **The baked result** goes into the existing `TerrainWeightMap` and weight PNGs, so the renderer, the macro albedo, GI and runtime builds are unchanged. A runtime build does not need to evaluate the rules.
- **Height edits**: when a sculpt stroke ends (and throttled during the stroke, around 10 Hz, in the dirty rectangle only), affected texels are re-evaluated. This reuses `MarkWeightsChanged(rect)` partial texture patching.

### Procedural foliage scatter

Placement settings that belong to the plant go in the **type asset** (`.plutofoliage` v3, with v2 still readable):

- `spacing` (minimum distance between instances of the type)
- `footprintRadius` (exclusion radius against other types)
- `minScale` and `maxScale` (already there)
- `alignToSlope` (0..1, replacing the bool, which reads as 0 or 1)
- `leanDegrees` and `sinkDepth`
- `maxSlope` and `altitude` range
- `clusterAmount` and `clusterScale`
- `randomYaw`

Settings that depend on the scene, meaning **where** a plant grows, go in a **scatter layer** owned by the procedural terrain component (below):

- the foliage type index
- a density `TerrainRuleStack`
- a density scale
- a priority
- a seed
- an include mask

**Algorithm** (deterministic, cell-local, so it can be incremental):

1. Scatter layers run in priority order (trees, then shrubs, then plants, then grass).
2. For each scatter cell, the candidate positions are a jittered grid at the type's spacing: `hash(seed, type, cellX, cellZ, i)` gives the jitter, choice, yaw, scale and tilt, as in `vegetation.py`. A Poisson-disk tile set is an alternative if the grid shows patterns. Each candidate has a stable **procedural ID**, `hash64(layerSeed, typeAssetRef, gridX, gridZ)`, with bit 63 set so it can never collide with sequential manual IDs.
3. A candidate is rejected when any of these holds:
   - its roll is at or above density × cluster;
   - it is outside the slope or altitude range;
   - it is inside the footprint of a higher-priority instance (checked against a per-cell **exclusion grid**: a spatial hash of placed instances and their radii);
   - it falls in a mask exclusion.
4. Overrides are applied: removed IDs are skipped, and moved IDs take their stored transform.
5. Results are written into `FoliageComponent` as ordinary instances.

**Instance origin**: `FoliageInstance` gains `origin` (`Manual | Procedural`). Regeneration replaces only procedural instances in the dirty cells. Manual instances, and the existing brush workflow, are never touched. The editor's remove and move tools record overrides on procedural instances instead of editing them destructively. Undo goes through `SceneEditHistory` as existing foliage edits do.

**Incremental update**: the dirty cells come from the height-edit rectangle, mask strokes, spline moves and rule changes (a rule change dirties that layer everywhere). Only those cells' render commands and `FoliageCollisionCell` signatures change. Physics already rebuilds per cell, and the render cache is a known whole-component invalidation (`docs/FOLIAGE.md`, "Current scope"). Patching render commands per cell is needed here and becomes part of Phase 3.

**Storage**: procedural instances are baked into the scene, using today's heavy-data path. Because generation is deterministic, a later option can store only the overrides and regenerate at load (Verdant has about 712k instances). That option is designed for (IDs and overrides) but not built in this milestone.

### Biomes

- A **`.plutobiome` asset** contains:
  - texture rules: for each terrain palette layer referenced by name, a rule stack contributing that layer;
  - scatter layers: a foliage type asset reference plus a density stack;
  - detail layers (see below);
  - an optional macro tint.
- A **`ProceduralTerrainComponent`** goes on the terrain entity. It holds:
  - the global seed;
  - the palette (the terrain's ≤ 8 layers);
  - an ordered list of biomes;
  - the **biome weight source**: painted biome mask PNGs (4 biomes per RGBA plane, up to 8) and/or a per-biome rule stack (for example altitude bands);
  - global masks (named R8 PNGs);
  - scene-level scatter layers that apply everywhere;
  - output wiring to the entity's `TerrainComponent` and `FoliageComponent`.
- **Blending**: biome weights are normalized per texel and blurred by a border width. The layer coverage of each biome is computed, and the per-layer weights are blended by biome weight. Foliage density is multiplied by biome weight, then a dithered threshold breaks up the borders so they do not form a straight line.
- **The 8-layer palette limit** remains for this milestone. Biomes share palette layers by name, and the editor warns when a biome references a layer that is not in the palette. Lifting the limit (per-texel top-4 index plus weight splatting, with all layers in one texture array) is listed under stretch work. It touches `TerrainLayers.slang`, the weight format and GL instruction limits.

### GPU detail grass

- A **detail layer** (inside a biome, or scene-level) contains:
  - mesh: a small model, or built-in generated blade cards;
  - material;
  - density per m² at full density;
  - a scale range;
  - colour variation (tinted toward the terrain macro albedo underneath, so the grass matches the ground);
  - draw distance and fade band;
  - wind response (reusing `FoliageWindResponse`);
  - cast shadows (default off);
  - a density rule stack.
- **Baked on the CPU** for each detail layer: an R8 **density texture** at weight-map resolution (same evaluator, overrides included). Because the GPU never evaluates rules, there is no CPU/GPU determinism problem.
- **New GPU input**: an R16/R32F **terrain height texture** plus the terrain transform, patched per dirty rectangle like the weight textures.
- **Per frame** (compute):
  1. A camera-centred clipmap of tiles is used, with coarser tiles and lower density further out.
  2. For each tile, a fixed candidate count is hashed (stable across frames, so there is no popping when the camera moves).
  3. Each candidate samples the density texture against its hash, samples height and normal, and takes its scale, yaw and tint.
  4. Frustum and Hi-Z culling are applied.
  5. Instances are appended to a storage buffer, and indirect arguments are written per LOD.
  6. Drawing is indirect through the foliage material and wind path.

  Storage buffers only, never the transient uniform arena (the Verdant uniform-arena exhaustion is in `jungle-scene-handover.md`). The work reuses the patterns in `GpuInstanceCulling` (indirect args, first-instance batching on Vulkan, fallback on GL).
- **Distance fade**: candidates thin out with a per-instance hash threshold toward the draw distance, and blades shrink before they disappear.
- **Interaction (Phase 5b)**: a small top-down "trample" render target follows the camera. Character and physics capsules are splatted into it each frame, and it decays over time. The grass vertex shader bends blades away from the stored direction.
- **GI and shadows**: detail grass does not cast VSM shadows by default (VSM page churn), contact shadows only. It is excluded from DDGI and voxel GI, and AO darkening at the base is in the shader.

### Editor

- **Terrain inspector**:
  - a "Procedural" section with seed, palette, biomes list and global masks;
  - per-layer Source (`Painted` / `Procedural`), with a rule stack list (add, reorder, enable, solo);
  - a "Regenerate" button (whole terrain or brush region).
- **Visualization**: a viewport overlay draws any rule stack, scatter density, biome weight, attribute (slope, cavity) or override coverage as a heat map. Solo on a rule shows that rule's output alone.
- **Brushes**:
  - mask paint;
  - biome paint;
  - layer override paint;
  - restore procedural;
  - foliage "exclude" (adds a mask stroke instead of deleting instances).
- **Live update**: re-evaluation is throttled while sculpting, and the full pass runs when the stroke ends. A status line shows "procedural: N cells pending".
- **Diagnostics**: extend `FoliageDiagnostics`. It reports types with procedural layers but no spacing, biome layers missing from the palette, density textures larger than budget, and instance counts per layer.

### Scripting

- `TerrainScriptApi`: add `BiomeWeights(position)`, `MaskValue(mask, position)` and `RegenerateRegion(min, max)` (editor and tools; runtime too, for gameplay that clears an area).
- `FoliageScriptApi`: `IsProcedural(instance)`, and removal that records an override so a regenerate keeps the felled tree gone.
- Rebuild the native hosts and ScriptCore together (AGENTS.md).

## Phases

Each phase ends with tests passing on **Vulkan and OpenGL**. Follow the build rules in the memory `test-suite-and-build-pitfalls` (one build at a time, known baseline failures).

| Phase | Deliverable | Tests |
| --- | --- | --- |
| **1. Core evaluator** | `TerrainAttributes`, noise (integer hash), `TerrainRule` and `TerrainRuleStack`, `DistanceFieldCache`, tile-parallel evaluation, serialization of rules as properties | `PlutoGEProceduralRuleTests`: determinism (golden hashes), each input and blend op, remap and curves, dirty-rectangle evaluation equals full evaluation |
| **2. Procedural texturing** | Layer `Source`, coverage semantics, override planes and PNGs, re-evaluation on height edits, inspector rule stack UI, heat-map overlay | Weight results for slope, altitude and cavity fixtures; override round trip; old scenes load identically (painted default); render shot of a slope-rock terrain on both backends |
| **3. Procedural scatter** | `.plutofoliage` v3 placement fields, scatter layers, stable IDs, `origin`, exclusion grid, overrides (remove and move), per-cell render-command patching, undo | Same seed gives the same instances; regenerating a region leaves outside cells and manual instances bit-identical; overrides survive regeneration; exclusion keeps the minimum distance; proxy promotion on a procedural instance still works (`PlutoGEFoliageProxyTests`) |
| **4. Biomes** | `.plutobiome`, `ProceduralTerrainComponent`, biome mask painting and rules, border blending and dithering, palette validation | Biome blend weights sum to 1; border foliage dithering is deterministic; a biome asset hot-reload re-evaluates only the affected outputs |
| **5. GPU detail grass** | Height texture, density textures, clipmap compute generation, culling, indirect draws, distance fade, wind; then 5b trample | Generation is stable for a fixed camera (instance count and checksum); no uniform-arena growth; Vulkan and GL render shots; benchmark using `--camera-travel` in a grass scene (frame counters, not `frame_ms`, since that varies about 30% between identical runs) |
| **6. Port the samples** | `forest.py` and `verdant.py` emit rule stacks, biomes, masks and spline references instead of instance lists and weight PNGs. Validates the model against real scenes and ends the "full regenerate overwrites editor edits" problem | Survival flow test, ThirdPerson headless suites compared with their baseline, Verdant instance-count parity within tolerance |
| **7. Docs** | `docs/PROCEDURAL_TERRAIN.md` (user guide), updates to `TERRAIN_LAYERS.md` and `FOLIAGE.md` | — |

Stretch work, after this milestone:

- more than 8 layers through index splatting;
- regenerate-on-load or streamed procedural instances (overrides stored only);
- flow and erosion attributes;
- a node-graph front end for `TerrainRuleStack`;
- multi-terrain or tiled worlds;
- navigation obstacles from scatter.

## Risks and open points

- **Scene size and edit cost**: baking all procedural instances keeps scenes large. Mitigated by the deterministic IDs, which allow store-overrides-only later. Per-cell render-command patching (Phase 3) is required or a slider drag will hitch.
- **Distance rules are global**: a spline edit can dirty a wide band. Distance fields need a radius cap (the rule's falloff bounds them).
- **GL instruction limits** (see the foliage transmission memory): the detail grass vertex shader and any terrain shader changes must be checked on GL early.
- **VSM and DDGI interaction** with dense detail: shadows off by default. Measure before enabling contact or VSM shadows for detail.
- **Coverage semantics** differ from the current painted normalization. Painted layers keep today's behaviour exactly, and only `Procedural` layers use coverage. Mixing both on one terrain needs clear inspector wording.
- **Physics**: a regeneration that touches many cells rebuilds those collision cells. Fine per cell, but a whole-terrain regeneration in play mode should be rejected or deferred.
