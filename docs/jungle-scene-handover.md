# Jungle scene: agent handover

State at 2026-11-03 of the work implementing [the jungle scene plan](JUNGLE_SCENE_PLAN.md): the Verdant Gorge
rainforest in ThirdPersonProject, and the engine work it needed. Read the plan first. This doc covers what is done,
what is broken or unverified, how to work on it, and what to do next, in order.

**One-line status.** M1 (scene with real assets) is built and reviewed in offscreen renders. Per-instance LOD,
vegetation LODs and persistent instance storage are in the engine. Directional-shadow instancing uses the instance
storage too, so a frame uses about 9.3 MB of the 32 MB Vulkan uniform arena, and the "uniform arena exhausted" error
in the editor viewport is fixed. The work up to that point is committed (engine `45e90be4` on `vulkanrhi`, sample
`6a41eb9`).

**2026-11-03 performance pass (uncommitted, engine and sample).** On the user's Vega 8 iGPU at the trailhead (Low,
1280×720), scene GPU time went from 1365 ms to 42 ms and the frame from 2.4 s to 57 ms (§3.3). The changes:
- Per-instance and per-cluster culling, and a triangle-density cap on per-instance LOD (`docs/FOLIAGE.md`).
- Triangle budgets (the plan's asset rules) with absolute vegetation LOD caps.
- Octahedral impostors for distant trees.
- GPU instance culling with occlusion. It is implemented and tested but off by default, because without an indirect
  draw count it costs more than it saves here (§3.10).

## 1. Repositories and uncommitted changes

Two repositories. Paths differ between the user's machines. On the Vega 8 machine the engine is
`C:\Users\Cameron.Airlie\dev\PlutoGE` (build tree `out/build/msvc18`) and the sample is `D:\dev\ThirdPersonProject`.
The user has their own uncommitted edits in the sample (Basin model metas, the project file). Leave those alone.

The tables below list the files of the committed work. The uncommitted 2026-11-03 pass adds:

| Area | Files |
| --- | --- |
| Triangle budgets | `engine/import/{include/PlutoGE/import,src}/MeshTriangleBudget.*` (new), `MeshImportOptions.h`, `MeshImporter.cpp` (budget pass, LOD caps), `ModelImportSettings.cpp` (`MODEL_TRIANGLE_BUDGET`), `ModelArtifactSettings.cpp`, `ContentBrowserPanel.cpp` |
| Impostor baking | `engine/import/{include/PlutoGE/import,src}/ImpostorBaker.*` (new), `engine/render/include/PlutoGE/render/ImpostorLayout.h` (new), `ModelImportService.cpp`, `AssetManager.{h,cpp}` (`.plutomesh` format 6) |
| Impostor rendering | `MeshImpostor.h` (new), `RenderCommand.h`, `RetainedRenderScene.cpp`, `FoliageComponent.{h,cpp}`, `engine/render/src/ImpostorDraws.*` (new), `RhiSceneRenderer.{h,cpp}`, `BasicRenderer.{h,cpp}`, `GeometryRendering.cpp`, `VirtualShadowMaps.cpp`, shaders `BasicLitImpostor.slang`, `ImpostorLayout.slang` (new) |
| Culling | `BasicRenderer.cpp` (cluster and per-instance culling, density cap), `engine/render/src/GpuInstanceCulling.*` and `shaders/InstanceCulling.slang` (new), `OcclusionCulling.{h,cpp}` (hierarchy view), `BasicLit.slang` (bin header lookup), `RhiRenderService.*`, benchmark `--gpu-instance-culling` and `instance_binning_ms` |
| Tests | `tests/ImpostorChecks.h`, `tests/GpuInstanceCullingChecks.h` (new), `InstanceLodChecks.h`, `LargeMeshOptimizationTests.cpp`, `ModelImportSettingsTests.cpp`, ctest `PlutoGE{Vulkan,OpenGL}{Impostor,GpuInstanceCulling}Tests` |
| Sample | `Tools/levelkit/models.py` (`ImportOptions`, `apply_import_options(force=...)`), `vegetation.py` (`imported_triangles`), `verdant.py` (tree shadow floors 2), `verdant_assets.json` (budgets, impostors), regenerated scene, reimported models |

**Engine** (branch `vulkanrhi`):

| Area | Files |
| --- | --- |
| Vegetation LOD profile (importer) | `engine/import/include/PlutoGE/import/MeshImportOptions.h`, `engine/import/src/MeshImporter.cpp`, `engine/assets/src/ModelImportSettings.cpp`, `editor/ui/src/panels/ContentBrowserPanel.cpp` |
| Per-instance LOD and instance storage (renderer) | `engine/render/src/InstanceStorage.{h,cpp}` (new), `engine/render/include/PlutoGE/render/BasicRenderer.h`, `engine/render/src/BasicRenderer.cpp`, `engine/render/include/PlutoGE/render/RhiSceneRenderer.h`, `engine/render/src/RhiSceneRenderer.cpp`, `engine/render/src/GeometryRendering.cpp`, `engine/render/shaders/BasicLit.slang`, `engine/render/shaders/GeometryDepth.slang`, `engine/render/cmake/NormalizeOpenGLShader.cmake`, `engine/render/CMakeLists.txt` |
| Foliage commands for tools | `engine/scene/include/PlutoGE/scene/components/FoliageComponent.h`, `engine/scene/src/components/FoliageComponent.cpp`, `tools/RenderSceneShot.cpp` |
| Tests | `tests/InstanceLodChecks.h` (new), `tests/LargeMeshOptimizationTests.cpp`, `tests/VulkanRhiTests.cpp`, `tests/OpenGLRhiTests.cpp`, `tests/CMakeLists.txt` |
| Docs | `docs/FOLIAGE.md` (per-chunk LOD, instance storage, vegetation LODs), this file |

**Sample:**

| Area | Files |
| --- | --- |
| Level kit | `Tools/levelkit/vegetation.py` (new), `Tools/levelkit/landscape.py` (`GroundPainter.start/blend/noise/...`), `Tools/levelkit/models.py` (`"lods": "vegetation"`, `Model.triangles`) |
| The level | `Tools/levels/verdant.py`, `Tools/levels/verdant_assets.json` |
| Generated | `Assets/Scenes/Verdant.plutoscene` (+ meta), `Assets/PostProcessing/Verdant.plutopostprocess` (+ meta), `Assets/Textures/Verdant/`, `Assets/Materials/Verdant/`, `Assets/Models/Verdant/` (31 Poly Haven models), `Tests/data/verdant_views.json`, `Tests/views/Verdant/` |
| Docs | `docs/LEVELS.md` (Verdant Gorge section), `docs/THIRD_PARTY_ASSETS.md` (credits, rewritten by `fetch_assets.py`) |

The project file's `ASSET` list is a cache the editor rewrites (the asset database rescans `Assets/`). New assets
need no manual registration.

## 2. What is done

### Scene (M1)

- **Content:** 1 km terrain, about 1 km² of forest, five places (trailhead, canopy trail, ruin clearing, gorge pool,
  ridge lookout). Its sky and clouds are copied from the Outpost, with tropical haze and broken cumulus.
- **Vegetation:** about 394,000 plants in ten layers: canopy trees, understorey trees, shrubs, broadleaf plants,
  ferns, grass, moss, deadwood, roots and rocks. They take 95 entities, because each FoliageComponent holds one
  layer at one LOD floor.
- **Generating and reviewing:** `python Tools/levels/verdant.py [--shots]` regenerates the scene in about a minute
  and renders review views into `Tests/views/Verdant/`. Those renders are the reference for the look; check them
  after any change.
- **Assets:** all CC0 from Poly Haven. The manifest is `Tools/levels/verdant_assets.json`; run
  `python Tools/fetch_assets.py Tools/levels/verdant_assets.json`. Fetching models needs the editor closed.

### Engine

| Plan item | What exists | Test |
| --- | --- | --- |
| E7-adjacent: vegetation LOD profile | `MeshImportOptions::vegetationLods` (`MODEL_OPTIONS` bit 8): a five-step chain down to 0.25%, prune-first, sloppy fallback. Editor checkbox "Vegetation LODs". | `PlutoGELargeMeshOptimizationTests` (leaf cards: standard 100%, vegetation 0.25%) |
| E4: per-instance LOD | Instanced draws carry `BasicInstanceLodChain`. Each frame, every instance of a chained draw is binned by its own LOD (in parallel), and each LOD draws once through an index buffer. Depth prepass and shading share the bins. | `--instance-lods` on Vulkan and GL (`PlutoGE{Vulkan,OpenGL}InstanceLodTests`) |
| E8, part: instance storage | `InstanceStorage`: records (two matrices) persist in 8 MiB storage pages. Retained arrays upload once. Per-draw constants go in a 32-byte uniform (set 3 binding 3, GL 18). Streamed objects keep the old 64-entry block (set 3 binding 0). The shader picks the path by `instanceRange.y` (`BasicLit.slang instanceObject`). | Covered by the instance-LOD and fog checks; see §3 |
| E9: scatter | In the level kit, not the engine: `levelkit.vegetation`, deterministic and rule-based (density functions, slope and height limits, clustering, spacing, size by height or width, trunk capsules, per-layer triangle budget → LOD floor grouping, spatial ordering) | Regeneration is deterministic |
| E13, hook | `VERDANT_DENSITY` / `scatter(density_scale=…)` thins every layer to a subset | — |
| Tools | `PlutoGERenderSceneShot` draws foliage as the runtime does (instanced, culled by draw and shadow distance). `FoliageComponent::GetRenderCommands()` exposes the cached clusters. | — |

## 3. Known problems (read before changing anything)

1. **Uniform arena: about 9.3 MB of 32 MB per frame at full density.** It is no longer close to the limit.
   - CSM shadow casters with more than one instance now read `InstanceStorage` records
     (`DirectionalShadow.slang shadowInstanceModel`, `ShadowInstanceStorage` at set 3 binding 3) and draw all
     their instances in one call.
   - What remains in the arena is mostly per-draw object and material uniforms for about 31k geometry draws.
     Point shadows and `OpaqueShadow.slang` still use the 64-matrix chunks.
   - **Threading rule:** shadow and geometry recording run as `ParallelRecording` worker jobs. Neither
     `InstanceStorage` nor `PersistentParameterCache` (`m_geometryParameters`) is thread-safe. Every record and
     uniform handle a job binds is prepared on the main thread before `ParallelRecording` starts
     (`shadowInstanceRecords`, `shadowInstanceRanges`, `shadowInstancePlaceholder`, `shadowStorageOffBlock` in
     `BasicRenderer.cpp`). Calling them from a job raced the geometry job and crashed or hung the runtime.
   - No breakdown by consumer exists yet. Add one (count bytes per `Acquire*` caller) before optimizing further.
   - The arena is `TransientUniformSize` in `engine/render/src/rhi/vulkan/VulkanDevice.cpp`. Do not just raise
     it: that hides the scaling problem.
2. **The full GPU suite has not passed since the instance-storage change.** An earlier full run overlapped a
   build and reported 29 failures that are not trustworthy. After the final fixes, only these were re-run, and they
   pass on both backends: `--fog-only`, `--instance-lods`, cloud tests, `PlutoGELargeMeshOptimizationTests`.
   Run `ctest -C Release -R "Vulkan|OpenGL" --timeout 600` on a quiet build, then fix what fails.
   - Known pre-existing failure: `PlutoGEVulkanDdgiTests` "cascades did not extend the volume" (far roof 0.2686 vs
     0.2595).
   - Likely suspects if others fail: any pipeline built from the instanced geometry descriptor that strips bindings
     (see the depth-variant filter in `GeometryRendering.cpp`), and any shader including `BasicLit.slang` that
     still indexes `instances[instanceId]` directly.
3. **Frame cost at full density (benchmark, 1280×720, High, on the machine the earlier work used; Basin is 12.5 ms
   there; superseded by the Vega 8 table below):**

   | Scope | ms |
   | --- | --- |
   | Whole frame | 127 |
   | Geometry | 103, of which the alpha-tested coverage depth prepass is 58 |
   | Shadow cascades | 14 (cascade 3 is 8.6) |
   | Post | 11 |

   Triangles are 88 M across about 4.5 instanced passes. The rest is draw count (31k) and alpha-tested overdraw.

   **On the user's Vega 8 iGPU** (2026-11-03; `--benchmark-project … 60 60 --frozen-render --quality=low`,
   1280×720, the startup camera at the trailhead), each step cumulative:

   | Step | Scene GPU ms | Coverage depth ms | Main pass ms | Draws | Instance draws | Opaque triangles |
   | --- | --- | --- | --- | --- | --- | --- |
   | Before (HEAD `45e90be4`) | 1365 | 533 | 831 | 27,419 | 1,767,931 | 64.4 M |
   | Skip instances outside the view | 1187 | 486 | 698 | 17,477 | 1,115,448 | 38.4 M |
   | Triangle-density cap (0.5 per pixel) | 506 | 174 | 328 | 17,345 | 1,115,448 | 35.3 M |
   | Skip instances beyond the draw distance | 400 | 141 | 254 | 2,354 | 36,670 | 23.7 M |
   | Triangle budgets, LOD caps, impostors (reimported) | 43 | 5.4 | 32.3 | 2,206 | 43,926 | 1.7 M |
   | Cull whole clusters first | **42** | 5.4 | 30.7 | 2,207 | — | — |
   | Leaf cards, generated leaf masks, tree budgets 60 k / 40 k | **52** | — | — | — | — | 2.6 M |

   **At 1920×1080, Low** (2026-11-03, same camera; internal 1129×635), each step cumulative:

   | Step | Frame ms | Scene GPU ms | Geometry GPU ms | Recording CPU ms | Binning ms | Opaque triangles |
   | --- | --- | --- | --- | --- | --- | --- |
   | Before | 62.5 | 59.5 | 40.7 | 52.5 | 8.6 (+2.3 preparation) | 2.27 M |
   | Culled clusters skipped before staging and recording | 63.8 | 62.2 | 43.4 | 20.4 | 1.8 | 2.27 M |
   | Foliage detail 0.6 (Low tier default) | **54.1** | **52.3** | 36.8 | 19.2 | 1.7 | 1.68 M |

   The frame is now GPU-bound. The shaded pass (about 30 ms) costs far more than the coverage prepass (6.5 ms) over
   the same triangles, so the next lever is fragment cost: the alpha test the shaded pass repeats after the prepass,
   and resolution.

   With cluster culling the frame is 57 ms (CPU-bound: 49 ms recording, of which instance binning is 10 ms). The
   benchmark CSV now has `instance_binning_ms`.

   "Instance draws" counts an instance once per submesh. A density cap of 0.25 measured the same as 0.5 (509 ms),
   because the instances it would coarsen are already at their last LOD, so 0.5 stays. The user's editor capture
   (455×358 viewport, before these changes) showed 314 ms GPU with 24.5 M triangles.
4. **Shadow passes do not use per-instance LOD.** They use the cluster LOD (from 128 m cell bounds, so fine),
   floored by `MinShadowLod`. The scene compensates with high shadow floors and short shadow distances.
5. **The vegetation LOD thresholds** (`kVegetationLodTargets` in `MeshImporter.cpp`) were picked by reasoning,
   not tuned on screen.
6. **Look issues** seen in the review renders:
   - The trees are Poly Haven island trees and jacaranda, temperate rather than tropical. Emergent rainforest trees,
     palms and vines are missing.
   - A flat grey disc sits at some tree bases; probably part of an asset.
   - The gorge walls still read pale in places.
   - The pool's contact foam is strong.
   - No wind.
7. **Verdant DDGI uses voxel rays.** Hardware rays rebuilt acceleration structures over all plants every frame
   (17.7 ms). Masked leaves are opaque to hardware rays anyway (plan E11).
8. **Resolved: canopy trees bottomed out at their coarsest LOD.**
   - Every Verdant model now imports under a triangle budget (`verdant_assets.json` `triangle_budget`; 25 k for
     trees). Vegetation LODs 3–5 have absolute model caps.
   - Canopy and understorey trees bake impostors (`"impostor": true`), drawn below 40 px.
   - Leaf thinning: budgeted leaves are fewer, enlarged cards (`MaxCardEnlargement` 1.75), which read slightly chunky
     up close. Mid-distance trees still thin out until E3 (coverage-preserving alpha mips).
   - Impostors read a little warmer and brighter than the lit meshes at distance; compare the aerial and lookout
     renders before tuning.
9. **Resolved: "Vulkan per-frame uniform arena exhausted" in large editor viewports.**
   - Cause: the editor's `Renderer::PrepareVisibleRenderCommands` copied each partly visible foliage cluster's
     instances into per-frame scratch arrays.
   - Effect: their records moved every frame, so uniform blocks cached by record offset (shadow instance ranges,
     the 64-instance chunk path) were minted afresh each frame. They filled the 16 MiB persistent region and spilled
     into the 32 MiB per-frame arena; bigger viewports had more partly visible clusters.
   - Fix: producer-retained arrays (`sourceObject` and `sourceRevision` set) now pass through whole, as in the
     runtime, and the RHI renderer culls their instances (`RetainedRenderSceneTests`).
   - The exhaustion error now lists the arena's largest consumers by buffer debug name (`UniformArenaReport` in
     `VulkanDevice.cpp`), so a recurrence says what to fix.
10. **GPU instance culling is off by default.** `GpuInstanceCulling` (count, scan and scatter compute passes, with
    occlusion against the previous frame's depth hierarchy) draws the same images as CPU binning on both backends
    (`--gpu-instance-culling` tests). Measured at the trailhead with cluster culling, though, it lost:
    - The passes take 0.7 ms.
    - Every LOD slot of every binned draw is an indirect draw, about 21 k per pass against CPU binning's 2.2 k
      non-empty draws. That raised scene GPU from 42 to 94 ms and recording CPU from 49 to 132 ms.
    - To make it pay: an indirect draw count in the RHI (`vkCmdDrawIndexedIndirectCount`,
      `GL_ARB_indirect_parameters`) to compact slots, and slots only for the LODs a cluster's distance range can
      reach. Then re-measure with `--gpu-instance-culling` against the default.
11. **Open: dark angular shapes in the aerial review render** (`Tests/views/Verdant/aerial.png`, on the ground near
    clearings and the gorge). They appeared with the triangle-budget reimport and stay in the same places.
    - Ruled out:
      - impostors (a render with the impostor shaders removed still shows them);
      - tree shadow LOD floors (4 → 2);
      - deadwood card enlargement (cap 2.5 → 1.75, deadwood budget 12 k);
      - clustered vegetation LODs (permissive retry before clustering, `VegetationLodVersion` 2);
      - deadwood and roots: beyond their draw distances from the 260 m camera.
    - Next: render the aerial view with one layer at a time (or one model's budget removed) to find the source.
      Ground-level views do not show them.
12. **Shading now dominates.** With geometry fixed, the main geometry pass (BasicLit shading at 720p on alpha-tested
    foliage) is about 31 of the 42 ms. Profile its lighting cost (DDGI apply, VSM fallback to CSM, subsurface) next.

## 4. How to work on it

- **Engine build:** tree `C:\Users\cam\dev\PlutoGE\out\build\msvc17`, Release. Close the editor first, because it
  locks `PlutoGEEditor.exe`.
  - Build: `cmake --build out/build/msvc17 --config Release --parallel --target <targets>`.
  - Building several targets that link `PlutoGERender` in one command can fail with `LNK1104` on
    `BasicRenderer.obj`. Rebuild the target alone.
  - `PlutoGEImportModel`, `PlutoGERenderSceneShot` and `PlutoGERuntime` must be rebuilt after engine changes; the
    level kit picks up the newest build.
- **Tests:**
  - `out/build/msvc17/tests/Release/PlutoGEVulkanRhiTests.exe --instance-lods` (and the OpenGL executable). Other
    flags are listed in `tests/CMakeLists.txt`.
  - The test executables hand-load each shader (`shaders.X = ReadSpirv(...)` / `ReadText(...)`). A new shader
    package must be added there, or the feature stays silently off in tests.
- **Regenerate and review:** from the sample root,
  `python Tools/levels/verdant.py --shots`, then look at `Tests/views/Verdant/*.png`. View yaw 0 looks north (−z)
  and 90 looks west.
- **Benchmark:**
  1. Make a scratch copy of `ThirdPersonProject.plutoproject` with `STARTUP_SCENE project://Scenes/Verdant.plutoscene`
     and `VSYNC 0` (for example `_Bench.plutoproject`; delete it afterwards).
  2. Run `out/build/msvc17/runtime/Release/PlutoGERuntime.exe --benchmark-project <abs path> <out.csv> 120 120`.
  3. Read `<out>.csv` (`frame_ms`, `uniform_bytes`, `geometry_draws`, `opaque_triangles`) and `<out>.gpu.csv`
     (scopes).
  4. Check stdout for "uniform arena exhausted": failed frames report zero draws.
  - Use `VERDANT_DENSITY=0.25` to compare.
- **GL pitfalls met here:**
  - Slang renames struct members that collide with other globals, and GL links blocks by member and block name. Give
    shared struct fields unique names, and wrap `StructuredBuffer<uint>` in a struct.
  - Set-3 bindings need explicit mappings in `NormalizeOpenGLShader.cmake`.
  - GL draws have no first instance; Vulkan's `SV_VulkanInstanceID` includes it.
- **Recorded buffer uploads** (`UpdateBuffer` on storage buffers) must happen outside rendering. `InstanceStorage`
  prepares and uploads in the block before `ParallelRecording` in `BasicRenderer::Render`.

## 5. Next steps, in order

1. **Make the editor viewport reliable.**
   - Done: CSM shadow instancing uses `InstanceStorage` records, and the arena error is gone (§3.1).
   - Measure uniform bytes per consumer, then move `OpaqueShadow.slang` and point shadows onto the records the
     same way.
   - Cut per-draw uniforms for instanced foliage (one draw per command and LOD is fine; the remaining waste is
     per-draw object and material blocks).
   - Exit when `uniform_bytes` is below 8 MB at full density and the editor opens Verdant cleanly.
2. **Pass the full GPU suite** on both backends, per §3.2.
3. **Draw count and overdraw.**
   - Done (2026-11-03):
     - per-instance and per-cluster view and draw-distance culling;
     - the triangle-density cap;
     - triangle budgets with absolute LOD caps;
     - impostors (E6);
     - GPU instance culling with HiZ (E8; opt-in, §3.10).
   - Next:
     - Make GPU culling pay (§3.10).
     - Profile the shading cost (§3.12).
     - Drop the editor's redundant scratch-array instance culling (§3.9).
     - Bin shadows by instance.
   - Exit at under 33 ms at High at the trailhead.
4. **E1 wind**, the biggest visual gap. Design notes:
   - Apply wind in the instanced vertex path (`BasicLit.slang instanceObject` / `transformSurfaceVertex`) and the
     shadow instanced shaders, from a scene `WindComponent` plus per-layer stiffness carried in the instance-storage
     block.
   - Static VSM pages will not move with wind; restrict wind to layers whose shadows are cascaded or contact-only,
     or invalidate their pages.
5. **E3** coverage-preserving alpha mips. Leaves thin out at distance; visible in the aerial and lookout renders.
6. **Assets:** tropical hero trees, palms and vines (Fab or ambientCG; check licences, see the plan §2). Retune the
   per-layer `max_triangles` budgets in `verdant.py`.
7. **The rest of the plan:** E2 texture compression, E5 translucency, E6 impostor refinements (frame blending,
   shadows from depth, an "impostor beyond" setting per foliage type), E10 16-bit heightmaps,
   E11 masked RT, E12 weather, and E13 quality scaling (the density hook exists; presets do not use it).

When something lands, update `docs/FOLIAGE.md` or the relevant engine doc, the plan's status, and this handover.
