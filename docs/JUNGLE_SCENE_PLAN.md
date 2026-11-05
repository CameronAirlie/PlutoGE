# Jungle scene plan

A dense rainforest showcase level for ThirdPersonProject, built from realistic third-party scans. It doubles as
the engine's stress test for heavy foliage. This plan covers the target, the asset pipeline, the engine work the
scene needs (ordered and scoped as independent modules), and how the scene is built and measured.

Status: planned, 2026-11-02. Engine capabilities and gaps were surveyed against the code on this date.

**Progress (2026-11-03):** M1 is built (Verdant Gorge, ThirdPersonProject). Per-instance LOD (E4), the vegetation
LOD profile, persistent instance storage (part of E8), level-kit scatter (E9) and a density hook (E13) are done.
Triangle budgets with absolute vegetation LOD caps, octahedral impostors (E6, first version) and GPU instance culling
with HiZ (E8, opt-in) landed on 2026-11-03. The scene renders at about 42 ms of GPU time on a Vega 8 iGPU at 720p Low.
See [the handover](jungle-scene-handover.md) for state, known problems and next steps.

## 1. Target

**Place.** "Verdant Gorge": about 600 × 600 m of playable rainforest inside a 1 km terrain. It has four zones:

| Zone | Character | What it exercises |
| --- | --- | --- |
| Canopy trail | Narrow path under a closed canopy; dark, shafts of light, mist | Dense foliage, shadowed volumetric fog, GI under occlusion |
| River gorge | Rock walls, roots, a still pool below a cascade | Rock scans, wet surfaces, water at one height |
| Ruin clearing | Overgrown stone ruins in an open clearing | Sky and cloud lighting, weather, open sight lines |
| Ridge lookout | View across the canopy to distant hills | Long draw distance, distant trees, cloud shadows on the canopy |

**Vegetation layers**, each with its own density and draw distance:

1. Emergent trees: 40–60 m, sparse, visible from everywhere.
2. Canopy trees: 20–35 m, closed in the trail zone.
3. Understorey: palms, tree ferns, young trees, 2–8 m.
4. Shrubs and broadleaf plants: anthurium-, calathea- and monstera-like, 0.5–2 m.
5. Ground cover: ferns, grass, moss, leaf litter, roots, deadwood.
6. Vines and hanging moss: cards and cloth.

**Look.** Humid haze and low mist. Light reads in layers: hard sun through canopy gaps, green-tinted bounce
under leaves, and backlit translucent leaves. A weather cycle moves between broken cloud and an overcast
downpour (docs/CLOUD_SHADOWS.md).

**Success criteria:**

- At High, the canopy trail holds 60 fps (16.6 ms) on the reference desktop GPU at 1440p with the upscaler at
  Quality. Every preset holds its budget (section 6).
- No visible LOD pops inside 30 m, and no alpha shimmer with TAA on.
- Wind moves every vegetation layer, and shadows move with it.
- VRAM stays under 3 GB at High.

## 2. Assets

### Sources and licences

| Source | Licence | Use for |
| --- | --- | --- |
| [Poly Haven](https://polyhaven.com) | CC0 | First choice. The sample's `levelkit/polyhaven.py` already fetches it. |
| [ambientCG](https://ambientcg.com) | CC0 | Ground, bark, moss and rock textures; leaf atlases. |
| [Fab](https://www.fab.com) (Megascans and others) | Fab Standard License | Hero plants and trees Poly Haven lacks. Before adding anything, check the current EULA and the account's entitlements. Bridge-era Megascans were licensed for Unreal Engine only. |
| Sketchfab CC-BY | Attribution | Only where nothing CC0 fits. Record credits in docs/THIRD_PARTY_ASSETS.md. |

Poly Haven models that match today, found through its API on 2026-11-02:

- Plants and shrubs: `anthurium_botany_01`, `calathea_orbifolia_01`, `pachira_aquatica_01`, `fern_02`,
  `nettle_plant`.
- Trees: `island_tree_01`–`03`, `jacaranda_tree`.
- Ground cover: `moss_01`, `grass_medium_01`/`02`, `grass_bermuda_01`.
- Deadwood and roots: `dead_tree_trunk`, `dead_tree_trunk_02`, `dry_branches_medium_01`, `pine_roots`.
- Rocks: `boulder_01`, `rock_moss_set_01`.

Emergent rainforest trees, palms, tree ferns, vines and lianas are the gaps. Fill them from Fab or ambientCG
leaf atlases on authored trunk meshes.

### Asset rules

Until compressed textures land (E2), every texture sits in VRAM as uncompressed RGBA8. A 2K map with mips costs
about 21 MB.

| Class | Texture size | Triangles (LOD0) | LODs | Notes |
| --- | --- | --- | --- | --- |
| Hero tree | 2K bark, 2K leaf atlas | ≤ 60k | 4 + impostor | Leaf cards, not modelled leaves |
| Canopy or understorey tree | 1K–2K | ≤ 25k | 4 + impostor | |
| Shrub or plant | 1K | ≤ 6k | 3 | |
| Ground cover | 512–1K atlas | ≤ 800 | 2 | Shared atlases per biome |
| Terrain layer | 2K | — | — | At most 8 layers, 4 per pixel |

- **Leaf textures:** an albedo with alpha and an OpenGL normal map. Use `flipNormalY` for DirectX sources. Add a
  roughness map, and a translucency or thickness map once E5 lands.
- **Authored LOD chains:** import them as LOD0–LOD3 through E7, not as separate meshes. The importer's
  auto-simplification destroys leaf-card silhouettes, so turn it off for cards.
- **Credits:** every asset goes into `Tools/levels/verdant_assets.json` and gets a credit row in
  docs/THIRD_PARTY_ASSETS.md.

## 3. What the engine has and lacks

These features carry over unchanged:

- Cell-instanced foliage with brush painting and trunk capsule collision.
- 8-layer terrain with height blending and anti-tiling.
- Alpha mask and two-sided materials, with approximate subsurface for the directional light.
- VSM shadows and contact shadows.
- Volumetric fog shadowed by VSM and the cloud shadow map, which gives shafts through canopy and cloud gaps.
- DDGI with the voxel source, which honours masks; SSGI; baked ambient volumes.
- Physical sky, clouds with temporal reconstruction, and cloud shadows driven by weather.
- Particles for rain, insects and spores.
- Audio environment volumes with a Forest preset.

These gaps block the target. The work packages in section 4 close them:

| Gap | Effect on a jungle |
| --- | --- |
| No foliage wind | A static forest reads as fake. The graph-based workaround loses culling and VSM. |
| No impostors or HLOD | Distant canopy costs full meshes, or it vanishes. |
| LOD per 32–64 m cell; MinRenderLod 2 and MinShadowLod 3 floors | Coarse LODs up close and pops at cell boundaries. |
| Instanced draws chunked at 64, CPU culled, never occlusion culled | Draw-call and CPU limits at 100k+ ground-cover instances. |
| No GPU texture compression or streaming | VRAM runs out with scan textures. |
| Plain alpha test; mips lose coverage | Leaves thin out and shimmer at distance. |
| Subsurface is directional-only, with no thickness map | Backlit leaves look uniform. Torches and local lights don't shine through leaves. |
| No procedural scatter rules | Hundreds of thousands of instances are impractical to hand-paint. |
| 8-bit heightmap import | Terracing on gorge walls. |
| Hardware-RT DDGI treats masked leaves as opaque | The understorey is over-darkened on the hardware ray source. |
| No rain or wetness | No downpour. |
| Presets don't scale foliage | Low and Medium can't hold a budget. |

## 4. Engine work packages

Each package is a self-contained module with one integration seam, its own documentation, and tests on Vulkan
and OpenGL. Every package follows the pattern used for clouds:

- Shared C++/Slang layouts in `*Layout.h` or `*Config.h`.
- One renderer-owned class per pass, like `CloudTemporalFilter`.
- Components registered in the ComponentRegistry and reached from C# through `RegisteredComponent`.
- Shaders that opt into features through include-time capability macros, like `PLUTO_CLOUD_SHADOW_ATLAS`.

The list is ordered by priority. P0 packages block the target and P1 packages are needed to meet it.

### E1 Foliage wind (P0)

**Status: done (2026-11-05).** See [Foliage wind](FOLIAGE_WIND.md). Differences from the design below:
- The wind constants travel in each pass's material or draw block, not the instance-storage block. Instance storage
  needs no change.
- VSM receives wind; graph vertex deformation still disables VSM.
- Voxel GI and hardware-ray DDGI light the rest pose, so swaying never forces a re-voxelization.
- Clouds, particles, cloth and fog keep their own wind settings for now. `BuildSceneWind` and `SceneWindVelocity` give
  them the shared field when they adopt it.

- **What:** a scene-wide wind field and per-type bending. There are three motions: trunk sway from height,
  branch flutter from a per-instance phase, and leaf flutter driven by UV or vertex colour.
- **Design:**
  - A `WindComponent` holds direction, strength, gustiness and gust scale. Clouds, particles, cloth and fog can
    read the same `SceneWind` state in place of their own wind settings, so one setting drives the whole scene.
  - Foliage types gain stiffness, flutter and a pivot height.
  - The shader function `foliageWindOffset` lives in `FoliageWind.slang`, and the forward, depth, VSM and voxel
    vertex shaders call it. Deformation is a pure function of position, instance seed and time, so every pass
    agrees and shadows move with the leaves.
  - Instance seeds come from the instance transform; storage doesn't change.
- **VSM:** animated casters mark their pages dynamic, using the existing per-caster invalidation. Types with wind
  off stay cached.
- **Tests:**
  - Shadow and forward passes deform identically, compared by depth equality on both backends.
  - With zero wind the result is bit-identical to today.
  - VSM page churn stays bounded.

### E2 GPU texture compression (P0)

- **What:**
  - BC1, BC3, BC4, BC5 and BC7 formats in the RHI (`rhi::Format`), for Vulkan and GL with an S3TC/BPTC check.
  - DDS keeps its mips and is no longer decoded to RGBA8.
  - The cooker compresses PNG, JPG and TGA into DDS: BC7 for albedo, BC5 for normals, BC4 for roughness and AO.
- **Seam:** `TextureManager` load path plus the artifact cache. Shaders do not change.
- **Gain:** about 4× less texture VRAM (8× with BC1/BC4).
- **Tests:** round-trip error bounds per format, and existing texture tests on both backends.

### E3 Coverage-preserving alpha mips and alpha to coverage (P0)

- **What:**
  - Mip generation rescales each mip's alpha to keep the base level's coverage at the material's cutoff
    (Castaño's method).
  - Alpha to coverage is optional when MSAA is on. Masked materials hash-dither their cutoff under TAA.
- **Seam:** mip generation (`NormalMipmaps`-style module `AlphaCoverageMipmaps`) and the material flag.
- **Tests:** a leaf card keeps within 5% of its LOD0 coverage at mip 4.

### E4 Per-instance LOD and fixed defaults (P0)

- **What:**
  - Select LOD per instance inside a cluster, binning instances into per-LOD instance lists when the cluster
    is built each frame.
  - Change the `MinRenderLod` and `MinShadowLod` defaults to 0, migrating existing scenes.
  - Dither-fade LOD transitions.
- **Seam:** foliage cluster submission (FoliageComponent render cache → Renderer). Mesh LOD data doesn't change.
- **Tests:** an instance at 10 m in a 64 m cell draws LOD0, and the existing foliage tests keep passing.

### E5 Foliage translucency (P0)

**Done, with E11's purpose:** see [Foliage transmission](FOLIAGE_TRANSMISSION.md). It goes further than this outline, into both GI ray
sources. Hardware rays pass leaves by chance rather than sampling alpha, so E11's texture any-hit is no longer needed for them to let light through.

- **What:**
  - An optional thickness/translucency map (a new `BasicSurfaceTexture` entry, so no new sampler budget on
    GL).
  - Transmission for the directional and local lights, with the cloud shadow and VSM applied.
  - A thin-surface mode for two-sided leaves that lights the back face with the front's normal.
- **Seam:** the surface layer in `SurfaceBrdf.slang` and `SurfaceLayers.slang`.
- **Tests:** a backlit card brightens with thickness, a point light shines through it, and energy stays bounded.

### E6 Octahedral impostors (P1)

- **What:**
  - A bake tool renders a tree into a 16 × 16 octahedral atlas of albedo, normal and depth.
  - An impostor LOD type blends between frames and casts shadows from its depth.
  - Foliage types gain an "impostor beyond" distance.
- **Seam:** `ImpostorBaker` (editor and tool) plus an `Impostor` mesh-LOD kind that the foliage submitter
  understands. It is reusable for rocks and buildings.
- **Tests:** silhouette and lighting match the mesh at the switch distance, within tolerance.

### E7 Authored LOD-chain import (P1)

- **What:** recognise `_LOD0`.. node suffixes, and Megascans and SpeedTree conventions, as one mesh's LOD chain.
  Keep auto-simplification for meshes without a chain.
- **Seam:** `MeshImporter` LOD assembly. Artifact version bump.
- **Tests:** an import fixture that has a chain.

### E8 GPU-driven instanced foliage (P1)

- **What:**
  - Instance buffers in storage buffers.
  - A compute pass culls per instance against the frustum and the existing HiZ, then picks LOD and impostor.
  - Indirect draws per (type, LOD).
  - This removes the 64-instance chunking and the per-cell CPU cost, and lets foliage be occlusion culled.
- **Seam:** a `GpuFoliage` module beside `GpuDrivenOpaque`, behind a foliage submission interface, with the
  CPU path kept as the fallback.
- **Tests:**
  - CPU and GPU paths produce identical images.
  - 200k instances render with a draw count that doesn't depend on instance count.

### E9 Procedural scatter (P1, engine milestone E05)

- **What:**
  - A deterministic, seeded rule set per foliage layer: density from terrain layer weights, slope and height
    ranges, minimum spacing (Poisson disk), clustering noise, exclusion volumes and splines, and parent–child
    placement (ferns around trunks).
  - Edits regenerate only the dirty cells.
- **Seam:** `FoliageScatterRules` data on FoliageComponent, a `FoliageScatter` library that both the editor and
  levelkit can drive, and incremental cell rebuild (engine milestone E06).
- **Tests:** determinism (the same seed gives the same instances), rule filters, and incremental-rebuild
  equivalence.

### E10 16-bit heightmaps (P1)

- **What:** 16-bit PNG and RAW import, plus binary height storage (in place of inline text) for large terrains.
- **Tests:** round-trip precision and migration of existing scenes.

### E11 Masked any-hit for hardware-ray DDGI (P1)

- **What:** alpha-tested BLAS geometry (non-opaque flag plus an any-hit alpha lookup through the material
  table), so leaves let light through.
- **Seam:** `RayTracingScene` geometry flags and `DDGIRayQueryTrace.slang`.
- **Tests:** a masked card occludes in proportion to its coverage.

### E12 Weather (P2)

- **What:**
  - A `WeatherComponent` drives the cloud components' coverage, density and wind (already scriptable),
    `SceneWind` (E1), rain particles, and a global wetness value.
  - Wetness darkens albedo and lowers roughness on upward-facing surfaces, with puddles on terrain layers by
    height.
- **Seam:** wetness is a frame parameter read by `SurfaceLayers.slang`. Weather is just a component that writes
  other components' properties.
- **Tests:** wetness response on both backends, and that weather drives clouds deterministically.

### E13 Quality scalability for foliage (P2)

- **What:**
  - A per-preset foliage density scale (drops instances deterministically by seed), plus draw-distance and
    impostor-distance scales.
  - Shadow caster distance per layer.
- **Seam:** `GraphicsQualityPresets` plus the foliage submitter.

### Later

- **Flowing water:** flow-mapped water ribbons on splines (spline roads already exist) for the gorge river.
- **Foliage storage:** binary foliage storage and per-cell streaming.
- **Interaction:** the player bending foliage, and leaf particles on impact.

## 5. Building the scene

The scene is built by a level script in the sample project, like Basin, so it can be regenerated.

1. **Blockout.**
   - Write `Tools/levels/verdant.py`, copying `basin.py`'s structure: a heightfield for the gorge, ridge and
     clearing (16-bit once E10 lands), terrain layer weights, ruins, a path spline and a collision pass.
   - Register `Assets/Scenes/Verdant.plutoscene` in `ThirdPersonProject.plutoproject`, and add a contract.
2. **Asset manifest.**
   - List surfaces, terrain layers and models in `Tools/levels/verdant_assets.json`, then run
     `Tools/fetch_assets.py`.
   - Export multi-mesh trees as static meshes for foliage.
   - Record credits.
3. **Vegetation.** Scatter each layer by rule (E9), or with `levelkit.instancing` until E9 lands. Budgets:

   | Layer | Instances in level | Visible at once |
   | --- | --- | --- |
   | Emergent and canopy trees | ~2,500 | ~600 |
   | Understorey | ~15,000 | ~3,000 |
   | Shrubs and plants | ~60,000 | ~8,000 |
   | Ground cover | ~300,000 | ~40,000 |

   Hand-place hero trees and vines at the trail and the clearing.
4. **Lighting.**
   - Physical sky with high turbidity.
   - A cloud deck at coverage 0.5, with weather between 0.35 and 1.
   - Low height fog with anisotropy around 0.7 for forward-scattered shafts.
   - DDGI on the voxel source (or hardware rays after E11), SSGI, contact shadows on the directional light,
     and AgX.
   - Bake an ambient volume for the Low and Medium presets.
5. **Audio.** Forest environment volumes, river and cascade emitters, and rain layers tied to weather.
6. **Gameplay pass.** Traversal over roots and logs, cover in the ruins, and navigation around trunk capsules.

## 6. Performance budgets and measurement

Measure with `PlutoGERuntime --benchmark-project <scratch project> out.csv 600 120 --quality=<tier>`, using fixed
vantage views (`Tests/run_scene_views.py`) at each zone. Track GPU scopes from the `.gpu.csv` output.

| Pass (High, 1440p) | Budget |
| --- | --- |
| Foliage and terrain geometry | 4.5 ms |
| Shadows (VSM plus foliage casters) | 3.0 ms |
| GI (DDGI and SSGI) | 2.0 ms |
| Volumetric fog and clouds (including temporal filters) | 1.5 ms |
| Screen-space effects (SSAO, SSR, contact shadows) | 1.5 ms |
| Upscale, post and UI | 1.5 ms |
| Headroom | 2.6 ms |

Low and Medium use the same views at 30 and 60 fps targets on the iGPU reference, with E13's density scaling.
Each engine package records before and after numbers on these views in its own doc.

## 7. Milestones

| Milestone | Contents | Exit criteria |
| --- | --- | --- |
| M1 Greybox | Terrain, ruins, path, lighting, existing Poly Haven plants hand-placed | Scene loads, traversal works, baseline benchmark recorded |
| M2 Foliage foundations | E1 wind, E3 alpha mips, E4 per-instance LOD, E2 compression | Wind on all layers, no shimmer, VRAM within budget |
| M3 Dense jungle | E9 scatter, E8 GPU foliage, full layer budgets | Section 6 budgets met at High |
| M4 Fidelity | E5 translucency, E6 impostors, E7 LOD chains, E11 masked RT | Distant canopy and backlit leaves pass visual review |
| M5 Weather and polish | E12 weather, E13 scalability, audio, gameplay pass | Every preset meets budget, the weather cycle plays end to end |

## 8. Risks

- **Licensing.** Fab terms and entitlements vary per asset. Keep the scene shippable from CC0 assets alone, with
  Fab assets as optional upgrades.
- **VSM with animated foliage** may churn pages. Mitigations: limit wind-animated casters by layer, and cast
  ground-cover shadows only through contact shadows.
- **Scatter counts** may outgrow scene text storage before binary storage lands. Keep generated instances in a
  separate section file, using scene streaming's additive sections.
- **Leaf-card overdraw** at grazing views. Mitigations: overdraw-optimized cards and `optimizeOverdraw` on
  import, measured with the geometry diagnostics.
