# Legacy OpenGL renderer removal plan

Investigated 2026-11-04. This plan removes the legacy OpenGL render-pass
pipeline. The Vulkan and OpenGL render hardware interface (RHI) backends stay.
OpenGL projects keep working, but they render through `OpenGLDevice`,
`RhiRenderService` and `RhiSceneRenderer`.

## Decisions

- **LPV and RSM:** removed. They have no RHI version. DDGI, SSGI and VCTGI cover
  the same ground.
- **World-space RmlUi texture surfaces and the advanced RmlUi effects** (filters,
  `backdrop-filter`, shader decorators): removed. Only the GL3 RmlUi backend
  rendered them.
- **`TexturePainter`:** removed. It painted into GL textures, so the RHI path
  never showed the paint.
- **Legacy per-pass CPU/GPU timings** in the editor and runtime profilers:
  removed along with the passes.
- **Baked lightmaps:** removed. No RHI path sampled them. The lightmap bake
  stage, its GL compute path, generated UV2 atlases, material lightmap fields,
  the GL-only `TextureManager` lightmap loaders and the OpenGL-host
  `TextureManager::SetWindow` call went; probe baking and the `uv2` channel stay.

## Findings

`render::Renderer` does two jobs:

- It runs the legacy pass graph.
- It is the scene-submission layer that every frame uses, Vulkan included. This
  covers the retained scene, the command and decal store, submission culling and
  `PrepareVisibleRenderCommands`.

Scene components, the editor, the runtime and `RhiSceneRenderer`
(`GetDecalCommands`) reach it through `Engine::GetRenderer()`. The submission
half has to be extracted before `Renderer.cpp` can be deleted.

The legacy pipeline still runs in these places:

- **Standalone runtime with an OpenGL project.** The whole frame is legacy
  (`runtime/src/main.cpp`, the `else` branch of `useVulkanRenderer`), including
  `RenderTarget` spatial upscaling.
- **Editor on an OpenGL host:**
  - Frame clear and swap go through `Renderer::BeginFrame/EndFrame`.
  - The scene viewport falls back to legacy when no project is open.
  - The game viewport falls back to legacy when `!m_useRhiPreview`.
  - IBL capture falls back to `CaptureSceneCubemap`.
- **`Renderer::Initialize` on every OpenGL host.** It installs the window resize
  callback and enables `GL_TEXTURE_CUBE_MAP_SEAMLESS`. The OpenGL RHI does
  neither yet.

Post-process effect classes are also the parameter and serialization model.
`RhiPostProcessAdapter` reads their settings through `dynamic_cast`. Only their
GL `Initialize`/`Apply` code is legacy.

Size of the removal:

- About 17.8k lines are legacy-only.
- About 9.4k lines of GL code come out of shared files.
- `glad.h` is included from 17 public render headers and reaches scene
  components and every RHI file through `Mesh.h` and `RenderCommand.h`.

Only two ctest entries are legacy-only (`PlutoGEPostProcessInitializationTests`
and `PlutoGEVctEmissionCoverageTests`). The 47 OpenGL RHI registrations stay, so
the test-suite saving is small. The build-time saving comes from the deleted
sources and from keeping `glad` out of public headers.

## Phase 0: make the legacy path unreachable

1. Give the runtime an OpenGL RHI branch. The Vulkan branch through
   `RhiRenderService` is mostly API-neutral.
   - The runtime's Spatial upscaler only existed in the legacy path; the Vulkan
     runtime already rendered at native resolution. Both backends now do, with
     a warning in the log and in Project Settings. The Game Viewport keeps its
     render scale as a bilinear stretch.
   - Follow-up (not part of this removal): a real RHI spatial upscaler pass
     with sharpening, driven by the existing render scale and sharpness
     settings.
2. Present and clear OpenGL hosts through `RhiRenderService::Present`. This
   covers the editor shell, the launcher, build progress, `EditorLoadingPresenter`
   and `ViewportPanel::ClearFrame`.
3. Wire `RhiRenderService::Resize` for OpenGL in `Engine::Initialize`.
4. Enable seamless cubemaps in `OpenGLDevice`.
5. Remove the editor fallbacks:
   - the no-project scene viewport (`EditorShell`)
   - the `!m_useRhiPreview` branches and native GL texture display (`ViewportPanel`)
   - the `CaptureSceneCubemap` fallback
6. Give RmlUi shutdown and `TextureManager::SetWindow` a new owner outside
   `Renderer`.

Exit criterion: `Renderer::Initialize`, `RenderFrame`, `BeginFrame` and
`EndFrame` have no callers. The OpenGL RHI and Vulkan suites have no new
failures beyond the known baseline.

Status (2026-11-04): done, uncommitted.
- The editor presents through `IEditorCompositor::Present`. The OpenGL
  compositor clears and swaps the default framebuffer.
- The runtime renders through `RhiRenderService` on both APIs.
- `Engine` no longer initializes `Renderer`, and resizes the swapchain on both
  APIs.
- The full suite (`ctest -j4`, OpenGL DDGI excluded) took 1432 s. Its failures
  were all failures that also occur at HEAD.

## Phase 1: extract the submission layer

1. Move the submission state and API from `Renderer` into a new
   `RenderSceneFrontend`:
   - `Submit*`, `Publish/RemoveRenderProducer`, `Begin/EndRenderSceneScope`
   - `PrepareVisibleRenderCommands` and the command views
   - decal commands, culling cameras, the post-process debug view and the stats
   - the `InstanceCullJob` pools
2. Point `Engine::GetRenderer()` (or a renamed accessor) at the new class.
3. Move `DecalCommand` and the profiler stats structs to neutral headers.
4. Point `MeshComponent.h`, `FoliageComponent.h` and `TerrainComponent.h` at
   `RenderCommand.h`.
5. Port the tests that use the submission layer:
   - MeshReplacement, PreparationCache, EditorFoliage
   - RetainedRenderScene, EngineGraphicsApi
   - EditorProfilerPanel, RuntimeProfiler

Status (2026-11-04): done. The phase also absorbed work planned for Phases 2
and 3. The legacy passes and the effects' GL code reached the pipeline through
`RenderContext` and `ctx.renderer`, so keeping them compiling would have needed
glue that the next phase deleted. Done here:
- `Engine::GetRenderScene()` returns `RenderSceneFrontend`, which holds
  `DecalCommand` and `RenderSceneStats`.
- Deleted `Renderer` and `src/passes/*`. The profilers show only the scene
  submission and visibility stats.
- Stripped `Initialize`/`Apply` and all GL state from the post-process effects
  and `IPostProcessEffect`. Deleted `ShaderPostProcessEffect`, LPV, RSM, the
  VCT voxelization GLSL and probe-sampling mirror, and the `VctCoverage`/
  `FogIntegration` configure steps.
- Removed the SSGI legacy parameters and the SceneComposite LPV/RSM debug
  views. Saved indices resolve through the option list stored with them, so
  older files keep their view.
- `PostProcessInitializationTests` keeps its texture-upload and Slang VCT
  checks. Those still drive the `Shader` class, so they are ported when
  `Shader` goes.

## Phase 2: delete legacy-only code

Items 2 and 3 of the original list (the legacy effect code and LPV/RSM) were
done in Phase 1, as was the passes deletion in item 1.

1. Delete the legacy rendering sources:
   - `GBuffer`, `RenderTarget`, `Shader`, `BuiltinShaders`, `Graphics`
   - `SpatialUpscaler`, `IUpscaler`, `UniformNames.h`, `IndirectDraw.h`
   - `TexturePainter`
   - Port the Slang VCT checks in `tests/VctProbeCacheChecks.h` and
     `tests/VctEmissionCoverageChecks.h` off `Shader` onto RHI compute, and
     move the editor viewports' `RenderTarget` size holders to plain extents.
4. Delete the `ShaderGraphEvaluation.h.in` configure step from
   `engine/render/CMakeLists.txt`.
5. Remove the RmlUi GL3 backend (`RenderInterface_GL3`), the world-surface FBOs,
   `RenderWorldSurfaces`, the legacy `RmlUiRuntime::Render`, and the WorldSpace
   RmlUi mode.

Status (2026-11-04): done, with these differences from the list above.
- `Shader` went with `BuiltinShaders`, which pulled forward Phase 3 item 5 and
  the ShaderGraph GLSL from item 8:
  - `Material::Bind`, the material's shader override and
    `MaterialConfig::compiledShaderGraph`
  - `RenderCommand::shader`
  - `CompileShaderGraphToGeometryShader`
  - the asset manager's per-material GLSL compile
  The draw-state sort key now groups by shader-graph program instead.
- `Graphics` is deleted too. Its global GL bind cache went stale whenever the
  RHI touched GL, which is what the editor's `ResetStateCache` workarounds
  papered over. The remaining GL texture uploads call GL directly.
- The VCT shader checks drive the Slang-generated GLSL through a test-only
  program helper (`tests/GlTestProgram.h`), not RHI compute. They test shader
  maths, and the RHI already runs the same shaders in its VCT suites.
- The editor viewports hold plain extents, and the texture paint tool is gone.
- `stb_image`/`stb_image_write` compile in one unit owned by the
  `PlutoGE::stb_image` target. The write implementation used to live in
  `TexturePainter.cpp`.
- RmlUi: the GL3 backend and its third-party sources are gone.
  - The `WorldSpace` canvas mode stays: native canvases render it on the RHI.
    RmlUi documents in that mode were already hidden on the RHI and still
    are; the Inspector now says so.
  - The RmlUi docs list what parses but does not render: filters,
    `backdrop-filter`, shader and CSS-gradient decorators, `box-shadow` and
    `mask-image`.

## Phase 3: strip GL from shared code

Items 1 to 3 were done in Phase 1.

1. **Effects:** drop `Initialize`/`Apply`, `RenderContext` and `RenderTarget` from
   `IPostProcessEffect`. Strip the GL members and code from the 17 mixed effects.
   Keep the parameters, settings structs and summaries.
2. **SSGI:** remove the legacy-only parameters (Step Size, Depth Weight, Normal
   Weight).
3. **SceneComposite:** remove the LPV Only and RSM Only debug views.
4. **Mesh:** remove the VAO/VBO/EBO, `Initialize`, `Bind`/`Draw*` and the GL
   re-uploads.
5. **Material and shaders:**
   - Remove `Material::Bind`, the shader override and
     `MaterialConfig::compiledShaderGraph`.
   - Remove `RenderCommand::shader` and the sort keys based on it.
   - Remove the legacy compile in `AssetManager`.
6. **Texture:** remove the GL texture handle, the GL factories and `Upload3D`.
   Add a neutral 2D/cube discriminator.
7. **`TextureManager`:** remove the GL uploads. They also run, redundantly, under
   the OpenGL RHI today.
8. **`ShaderGraph`:** remove the GLSL emission.
9. **Scene:**
   - GPU particle buffers in `ParticleSystemComponent`
   - GL shadow textures in `LightComponent`
   - GL cubemap code in `IblCaptureComponent`
   - the `ColorVolume` upload in `Scene.cpp`
10. **Editor:** remove `EditorTextureDescriptor::nativeOpenGlTexture`, the
    `ReadTexturePixels` GL fallback and the `Graphics::ResetStateCache`
    workarounds.
11. **Build:** remove `glad` from all public headers. It stays a private
    dependency of `OpenGLDevice`, the ImGui OpenGL compositor and `SceneBaker`.

`SceneBaker.cpp` keeps its raw GL bake compute. It is separate from the legacy
pipeline and is out of scope here.

## Phase 4: data, tests and docs

1. **Data:** remove the LPV and RSM entries and the LPV Only/RSM Only debug-view
   values from the RocketLeg scenes and presets. Loading skips unknown effects,
   but inline camera stacks drop them silently when the scene is re-saved.
2. **Tests, delete:** `PostProcessInitializationTests`. Port its Slang VCT
   energy-conservation and probe-cache checks to RHI compute.
3. **Tests, trim:** remove the legacy parts of these tests:
   - the GL block in `ShaderGraphRenderingChecks.h`. This should fix the known
     `PlutoGEOpenGLShaderGraphTests` baseline failure.
   - the outline check in `OpenGLRhiTests.cpp`
   - the OpenGL branch of `EditorLoadingScreenTests`
   - the `opengl` variant of `TextureRefreshTests`
   - `FakeEffect` in `CameraStackTests`
   - the stray `Renderer.h` includes
4. **Docs:** update the docs that mention the legacy renderer, the RmlUi
   authoring guide and its pinned inventory. They currently list GL3-only
   filters and world surfaces.

## Measuring

- **When:** record a clean build of `PlutoGERender`, a clean full build, and
  `ctest -j` wall time before Phase 0 and again after Phase 3.
- **Baseline (HEAD `cdac46df`, msvc18 Release, `--clean-first --parallel`):**
  `PlutoGERender` and the engine libraries it depends on rebuilt in 581 s. The
  most recent `CTestCostData.txt` sums to about 452 s of serial test time.
- **After Phase 1 (`a2da4812`):** the same rebuild took 519 s (-11%), and
  the Release render objects total 59 MB.
- **After Phase 2 (`36d8ad72`):** 510 s (-12% from the baseline), with 98
  render objects totalling 57 MB. The clean rebuild time is now dominated by
  the large RHI units (`RhiSceneRenderer`, `BasicRenderer`, `VulkanDevice`)
  and the engine libraries it rebuilds with it.
- **Baseline failures:** compare against the known failing suites at HEAD.
- **Skip:** don't run the OpenGL DDGI suite on the Vega 8 iGPU, because it
  crashes the driver.
