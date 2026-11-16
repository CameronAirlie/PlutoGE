# Detail grass

Dense ground cover (grass, dry tufts) that the GPU generates around the camera every frame, instead of placed foliage
instances. Nothing per clump is stored in the scene: a clump is a pure function of its lattice point, so the same
clumps come back wherever the camera returns, and a field of millions of potential clumps costs a few kilobytes of
settings.

Use foliage (`docs/FOLIAGE.md`) for things that are individually authored or interactive (trees, bushes, rocks). Use
detail grass for the carpet underneath them.

## Authoring

Add **Rendering → Detail Grass** to an entity that has a `TerrainComponent`. A new component has one Meadow layer that
grows everywhere.

### The inspector

- **Summary**: roughly how many clumps and triangles the enabled layers put in view, or a warning when the entity has
  no terrain.
- **Layer list**: one row per layer, with an enable box, its healthy and dry colours, its name, and the terrain layer
  it grows on. A warning marks a layer whose terrain layer does not exist, because such a layer grows nothing.
  - **Add Layer** creates a layer from a preset.
  - The buttons duplicate, move or remove the selected layer, and a row's right-click menu does the same.
  - Each is one undo step. A duplicate gets a new seed so it does not grow on top of the original.
- **Preset** buttons replace a layer's size, colours, shading, wind, clump and distances. They keep its name, where it
  grows and its seeds. The presets are Meadow, Lawn, Tall Grass, Dry Tufts and Weeds (`render::DetailGrassPreset`).
- **Sections**, ordered by how often they are tuned:
  - **Where It Grows** picks the terrain layer from a list (*Paint Where It Grows* starts the terrain tool's Paint brush
    on that layer), the paint threshold and maximum slope, an optional world **height band** (*Limit Height*: between two
    heights, thinning out over *Height Fade* metres beyond them, for beaches and snow lines), and **Bare Patches**
    (noise at the colour patches' scale that leaves gaps in the cover). All of them are evaluated on the GPU as clumps
    are generated (`DetailGrassGenerate.slang`), so nothing is baked.
  - The Terrain Editor's **Grass** section shows the same settings for a terrain, or for every tile of a terrain world.
  - **Colour** shows a strip of blades with the dry patches and variation. Colours are shown as they appear on screen
    and stored as linear albedo.
  - **Clump Shape** previews the clump at each LOD; drag the preview to turn it.
  - **Distance And Cost** graphs the share of clumps kept against distance, with the LOD bands. Hover the graph for a
    readout. A table gives each LOD's clumps and triangles in view (`render::EstimateDetailGrass`).

Each layer (`Layer.N.*` in the scene file, up to eight) is one kind of cover:

| Group | Settings | Notes |
| --- | --- | --- |
| Where | `DensityLayer`, `WeightThreshold`, `MaxSlope` | The painted weight of the named terrain layer is the density. Below the threshold nothing grows; above it the density ramps to full. Empty grows everywhere. Holes in the terrain never grow grass. |
| Water | `GrowUnderWater` (off by default), `ShoreMargin` | Off: no grass under lakes, rivers or the sea (their still level, [Water](WATER.md#foliage-and-water)); it stops `ShoreMargin` metres above the waterline (negative reaches into the shallows) and fades in over 15 cm. The component measures the still water over every terrain sample (`DetailGrassHeightField::waterDepths`, packed after the samples in the same GPU buffer) and re-measures only when the water's still surface or the terrain moves, never as waves animate. |
| How much | `Density` (clumps/m²), `Height`, `Width`, `AlignToGround`, `Seed` | Density is near the camera. Height is metres, width a scale of the clump mesh. |
| Distance | `FullDensityDistance`, `DensityFalloff`, `DrawDistance`, `WidthCompensation`, `LodDistances` | Every candidate grows within the full-density distance. Beyond it the kept share falls as `(full / distance)^falloff`; 2 keeps the clumps per screen area constant. Thinned clumps widen (up to the compensation) so far grass keeps its coverage, and the whole field shrinks away over the last fifth of the draw distance. |
| Look | `Color`, `DryColor`, `DryAmount`, `PatchSize`, `ColorJitter` | World-space patches of dry and healthy grass, and per-clump brightness variation. Colours are linear albedo. |
| Shading | `GroundTint`, `RootOcclusion`, `NormalBend`, `Roughness`, `Translucency` | See "Why it looks like grass" below. Roughness and translucency only apply when the layer names no `Material`. |
| Wind | `Wind`, `WindStiffness`, `WindFlutter` | The scene's `WindComponent` bends the grass as it bends foliage (`docs/FOLIAGE_WIND.md`). |
| Clump | `Blades`, `ClumpRadius`, `BladeWidth`, `MinBladeHeight`, `Lean`, `Curvature`, `ShapeSeed` | The generated clump mesh (below). Wide, leaning clumps that overlap their neighbours read as a sward; narrow upright ones read as rows of tufts. |
| Surface | `Material` | Optional material asset. Without one the clumps use a white, two-sided, translucent material and take their colour from the layer. With one, set the layer colours to white to keep the material's colours. |

Painting the terrain's layers repaints the grass immediately: the density and ground colour are the terrain's own
weight maps and macro albedo, which the renderer already keeps on the GPU.

## How it works

```
DetailGrassComponent ──► RhiSceneRenderer (RhiDetailGrass.cpp) ──► BasicRenderer::SetDetailGrass
  layers, terrain          one BasicDetailGrassField per layer:         │
                           drawn heights, weight map + channel,         ▼
                           macro albedo, material                DetailGrassRenderer (per frame)
                                                                   Prepare: lattice, uploads, empty draws
                                                                   Record:  DetailGrassGenerate.slang
                                                                            ─► clumps + indirect counts
                                                                          │
                     coverage depth + geometry passes ◄─────────────────────┘
                     (BasicLitGrass.slang vertex stages, standard BasicLit fragments)
```

- **`PlutoGE/render/DetailGrass.h`**: the backend-neutral parts: `DetailGrassLayer` (settings), `DetailGrassClumpShape`
  and `BuildDetailGrassClump` (the mesh), `DetailGrassHeightField` (the terrain surface), and `DetailGrassLattice`, the
  layout the GPU walks, with a CPU reference of its selection. `DetailGrassLayout.h` holds the constants C++ and Slang
  share.
- **`scene::DetailGrassComponent`**: the layers, and the terrain's drawn surface
  (`TerrainComponent::GetSurfaceSampleHeight`, the smoothed heights the mesh draws), shared and rebuilt only when the
  terrain's heights, holes, grid or smoothing change.
- **`RhiDetailGrass.cpp`**: turns the scene's components into `BasicDetailGrassField`s, resolving textures and materials
  through the renderer's caches.
- **`DetailGrassRenderer`** (owned by `BasicRenderer`): uploads terrain samples per height field revision, keeps the
  clump meshes, and runs the generation pass after instance culling, outside rendering. It builds one `BasicDraw` per
  field, carrying `BasicDraw::detailGrass`.
- **`BasicLitGrass.slang`**: the standard material path with its own vertex stages, selected by `GeometryPipeline` like
  the terrain and impostor variants. Its fragments are BasicLit's, so grass receives every light, shadow, GI and
  screen-space effect the rest of the opaque scene does. `PLUTO_DETAIL_GRASS` adds one varying (`grassTint`) and
  applies it in `shadeSurface`; no other variant changes.

### The lattice

Candidates sit on a regular lattice of `1 / sqrt(Density)` metres in the terrain's local XZ. Each lattice point has a
**level** (how many times both coordinates halve) and a **rank** in [0, 1): points of level L rank in
[4^-(L+1), 4^-L), so thinning removes the finest levels first. A point grows while its rank is below the share kept at
its distance (times the density map and slope factors). It is jittered within half its own level's spacing, so every
level is uniformly spread.

The kept share quarters once per **ring**. Only points of level k or more can grow in ring k, so ring k dispatches
just every 2^k-th point. With falloff 2, each ring covers twice the radius of the last with a quarter of the density,
and every ring dispatches about as many candidates as the first: a 85 m field costs about as much to generate as a
20 m one. Each candidate is evaluated by exactly the ring its distance falls in, and the result never depends on the
ring, so nothing pops as the camera moves and rings slide (`DetailGrassTests` checks this against a brute-force walk).
A clump nearing its rank shrinks into the ground instead of vanishing.

A field is capped at 2M candidates per frame: denser or wider settings spread the lattice instead.

### The generation pass

`DetailGrassGenerate.slang` runs one thread per candidate (8×8 tiles, one dispatch per field with a Z slice per ring).
A surviving clump is culled against the view's side planes (a sphere padded by this frame's wind reach), its LOD
chosen by distance, and it is appended to its LOD's region with an atomic increment of that LOD's indirect draw. Each
region holds the most clumps the rings that reach its distances could produce, so it never overflows. A clump is 32
bytes: root, yaw, ground normal, height and width (halves), and its own and the ground's colour.

Nothing is read back to decide anything. The counts reach the CPU frames later, as diagnostics
(`BasicRendererFrameStats::detailGrass`).

### The clump mesh

`BuildDetailGrassClump` fans tapering blades around a root: one unit tall, horizontal sizes in metres. LOD 0 has the
shape's blades in four segments; LOD 1 about 60% of them in two; LOD 2 about a third as single triangles, wider so the
clump keeps its coverage. Every LOD draws a prefix of the same blades. Blade normals tilt across the blade, so it shades
as a rounded leaf.

## Why it looks like grass

- **Bent normals** (`NormalBend`): blade normals are turned to face the viewer, then bent toward the ground's normal.
  Blade-accurate normals light thousands of thin cards as noise; bent ones shade the field as one soft canopy.
- **Ground tint and root occlusion** (`GroundTint`, `RootOcclusion`): blades take the terrain's macro colour toward
  their roots and are darkened there, which stands in for self-shadowing within the sward and grounds each clump.
- **Patches and jitter**: low-frequency world-space noise mixes healthy and dry grass; each clump varies in brightness,
  height and width.
- **Translucency**: the default material transmits light through the blades (`docs/FOLIAGE_TRANSMISSION.md`), so
  backlit grass glows.
- **Shadows**: grass receives shadows (trees darken the meadow) but casts none into shadow maps, which would churn
  virtual shadow pages every frame. Screen-space contact shadows and SSAO add the small-scale shading.

## Cost

Measured on SurvivalGame's forest (two layers: a 22 clumps/m² meadow to 85 m, sparse dry tufts to 55 m), 1080p, RTX
4070 SUPER, camera travelling, `--quality=high`: the frame went from 10.56 to 11.83 ms of GPU time. Generation is
about 0.04 ms, coverage depth +0.16 ms, and the rest is shading the grass pixels (which also hide terrain pixels that
no longer shade). The CPU cost is building one draw per layer.

Ways to make it cheaper if a scene needs it:

- fewer blades per clump, or a shorter draw distance;
- `DensityFalloff` above 2 thins the distance faster;
- occlusion: the generation pass does not yet test the previous frame's depth hierarchy (`GpuInstanceCulling` shows
  how). Behind hills and buildings, hidden clumps still pay the prepass.

## Limits and next steps

- Only the main view draws grass (`RhiSceneRenderer::SetSceneEffectsEnabled`, as particles and oceans).
- The clump is always the generated blade mesh. A layer naming a model (flowers, clover) would need its LODs mapped to
  `DetailGrassClumpMesh::lods`.
- No trampling yet: a camera-following render target of characters' capsules that the vertex stage bends grass away
  from (`docs/dev/PROCEDURAL_TERRAIN_PLAN.md`, Phase 5b).
- Grass is invisible to GI (DDGI and voxels): it would add noise and cost for little light.
- The terrain's world transform may rotate and translate it. Non-uniform scale stretches the lattice and normals
  slightly.
- The inspector's estimate assumes a camera on fully painted, flat ground. The renderer's real clump counts
  (`BasicRendererFrameStats::detailGrass`) are not yet shown in the editor.

## Tests

- `PlutoGEDetailGrassTests` (CPU) covers:
  - the lattice's layout and rings, and exactly-once evaluation against a brute-force walk;
  - density thinning and the clump mesh at every LOD;
  - presets, which keep placement, and the estimate, checked against the closed form and the candidate cap;
  - component round trip and sanitizing, and the height field following its terrain.
- `PlutoGEDetailGrassInspectorTests` draws the inspector (`ui::DrawDetailGrassInspector`, through a test host) in a
  hidden OpenGL window with every section open. An untouched inspector must report no edits. With an output prefix
  argument, it writes screenshots for visual review.
- `PlutoGE{Vulkan,OpenGL}DetailGrassTests` (`--detail-grass`): a half-painted flat terrain grows grass only where it is
  painted. Unchanged views render and generate identically, calm wind changes nothing, a disabled layer draws nothing,
  and a small camera step keeps the field.
