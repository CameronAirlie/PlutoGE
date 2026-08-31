# RHI Implementation Agent Handoff

## Objective

Continue migrating PlutoGE rendering to the backend-neutral RHI, with Vulkan scene output visible inside the editor and eventual removal of the OpenGL-owned editor/presentation path. Preserve OpenGL compatibility until Vulkan reaches feature parity.

Read these documents first:

- `docs/rhi-implementation-plan.md`
- `docs/rhi-editor-migration.md`

## Current user-visible state

- The editor viewport can render the loaded scene through Vulkan `BasicRenderer`.
- Vulkan currently renders off-screen, reads RGBA pixels back to the CPU, and uploads them into an OpenGL texture displayed by ImGui.
- The viewport overlay reports the selected RHI, scene-command count, draw count, and changed-pixel count.
- Meshes, submeshes, LOD ranges, instances, bind-pose skinned geometry, and terrain base geometry are submitted.
- Albedo textures, material tint, UV scale, one directional light, camera-aware specular, ambient light, metallic/roughness factors, emission, and alpha masking are implemented.
- `D:\PlutoProjects\CoD\CoDplutoproject.plutoproject` currently contains `GRAPHICS_API Vulkan`.
- MSI Afterburner will still identify the editor window as OpenGL. That is expected until the GLFW window, swapchain, and ImGui presentation migrate to Vulkan.

## Completed architecture

- Backend-neutral RHI types and interfaces.
- Opaque generational resource handles.
- RAII wrappers for buffers, textures, samplers, and pipelines.
- OpenGL and Vulkan device implementations behind the same interfaces.
- VMA-backed Vulkan buffers and images.
- Vulkan dynamic rendering, descriptors, synchronization, layout transitions, and texture readback.
- Project-manifest graphics API selection, with OpenGL as the compatibility default.
- Offline Slang compilation to GLSL and SPIR-V.
- Shared `BasicLit.slang` and backend-neutral `BasicRenderer`.
- Vulkan availability probing and safe fallback to OpenGL.
- Project graphics API is passed into viewport creation instead of being hard-coded in the panel.

## Current worktree changes

The following material-texture slice is implemented in the working tree but has not been committed:

- `engine/render/include/PlutoGE/render/BasicRenderer.h`
  - `BasicVertex` includes a tangent with a safe default.
  - `BasicDraw` includes normal, metallic, and roughness texture handles.
  - Includes texture-channel selection and normal-Y flipping.
- `engine/render/src/BasicRenderer.cpp`
  - Expanded material uniform packet.
  - Four material sampler bindings: albedo, normal, metallic, roughness.
  - Neutral fallback normal and data textures.
- `engine/render/shaders/BasicLit.slang`
  - Tangent-space normal mapping.
  - Metallic/roughness texture sampling and channel selection.
  - Defensive tangent construction for geometry without valid tangents.
- `engine/render/cmake/NormalizeOpenGLShader.cmake`
  - Normalizes the additional OpenGL sampler bindings to slots 10-12.
- `engine/render/src/rhi/vulkan/VulkanDevice.cpp`
  - Vulkan material descriptor set expanded for four sampled textures.
- `editor/ui/src/panels/ViewportPanel.cpp` and its header
  - Pass mesh tangents into `BasicRenderer`.
  - Upload albedo as sRGB.
  - Upload normal/metallic/roughness textures as linear UNORM.
  - Keep separate sRGB and linear caches and prune inactive resources.
  - Populate texture channels and `flipNormalY` in neutral draw packets.

Important: some changes are staged and some are unstaged (`MM` status). Do not reset or overwrite either set. Review both `git diff --cached` and `git diff`.

## Verification status

Release editor build succeeds:

```powershell
cmake --build build --config Release --target PlutoGEEditor
```

Focused test targets:

```powershell
cmake --build build --config Release --target PlutoGERhiTypesTests PlutoGEOpenGLRhiTests PlutoGEVulkanBootstrapTests PlutoGEVulkanRhiTests
ctest --test-dir build -C Release --output-on-failure -R "PlutoGE(RhiTypes|OpenGLRhi|VulkanBootstrap|VulkanRhi)Tests"
```

Current results:

- `PlutoGERhiTypesTests`: pass
- `PlutoGEVulkanBootstrapTests`: pass
- `PlutoGEVulkanRhiTests`: pass
- `PlutoGEOpenGLRhiTests`: fail with `BasicRenderer cube produced a blank center pixel`

The OpenGL test reports no GL error and shader compilation/linking succeeds. Vulkan renders the same expanded renderer packet successfully. Do not weaken the test; identify the compatibility regression. Likely investigation points:

1. Compare pre/post-PBR generated GLSL and vertex/fragment interface behavior.
2. Validate every OpenGL uniform-block offset and sampler binding using GL reflection.
3. Read back/count the whole render target to distinguish a blank draw from a center-pixel/projection shift.
4. Confirm test geometry contains the expected tangent/default bytes and vertex stride.
5. Validate framebuffer, depth state, and texture completeness after binding slots 9-12.

The Debug editor executable currently fails to link because the build combines release CRT objects with the debug Assimp library. This is an existing CMake/configuration issue. Use Release for RHI validation unless fixing the build configuration deliberately.

## Immediate next work

### 1. Finish and verify the PBR texture slice

- Fix the OpenGL compatibility regression.
- Rebuild all four focused tests.
- Launch the CoD project and visually verify:
  - normal maps affect surface lighting;
  - packed metallic/roughness channels are correct;
  - no road/terrain sparkle or excessive aliasing returns;
  - missing maps correctly use neutral fallbacks.
- Add targeted tests for material uniform layout and multiple texture bindings if practical.

### 2. Extract editor rendering ownership

`EditorSceneRenderService` owns the selected RHI device, scene renderer, and uploaded-asset caches. `ViewportPanel` receives only an RHI texture reference and passes it to the editor compositor; it does not own or translate backend-native resources.

Introduce an editor-owned render service above panels with:

- selected `GraphicsApi` and device factory;
- `IRenderDevice` ownership;
- scene renderer ownership;
- mesh and texture upload caches;
- backend-neutral viewport image registration/presentation interface;
- explicit initialization, resize, frame submission, and shutdown lifecycle.

Keep scene submission backend-neutral. Do not move Vulkan or OpenGL handles into editor panel APIs.

Suggested boundary:

```text
EditorShell
  -> EditorRenderService
       -> IRenderDevice
       -> BasicRenderer / future SceneRenderer
       -> RenderAssetCache
       -> ViewportPresentation
  -> ViewportPanel (camera/input/UI only)
```

First extraction should be mechanical: move existing device/renderer/cache behavior without altering output. Follow with focused lifecycle tests.

### 3. Next scene-rendering features

After ownership extraction, implement in this order:

1. Point and spot-light packets and shader evaluation.
2. Shadow-map texture/pass infrastructure and directional shadows.
3. Transparent blending and sorted transparent draws.
4. Environment/IBL support.
5. Post-processing parity.
6. Animated skinning rather than bind-pose geometry.

Do not port the entire legacy renderer directly. Add small backend-neutral render packets and passes with explicit resource ownership.

### 4. Native Vulkan editor migration

Once scene rendering is sufficiently useful:

1. Create the GLFW window with `GLFW_NO_API` for Vulkan projects.
2. Implement Vulkan surface, present queue, swapchain recreation, and frames in flight.
3. Add an ImGui backend abstraction and Vulkan implementation.
4. Implement the Vulkan editor compositor and register Vulkan viewport images directly with ImGui.
5. Keep compositor registration device/API checked so cross-API fallbacks cannot be reintroduced.
6. Confine remaining OpenGL code to `rhi/opengl` until parity is proven.

## Known missing rendering features

- Point and spot lights.
- Shadows.
- Transparent material blending.
- Environment maps and image-based lighting.
- Full physically based BRDF parity.
- Post-processing parity.
- GPU skinning/animation.
- Native Vulkan editor swapchain and ImGui rendering.
- Vulkan ImGui compositor support; the viewport service now exposes a native RHI image and deliberately rejects cross-API registration.

## Engineering constraints

- Public RHI headers must not include GLAD, Vulkan headers, or expose native handles.
- Preserve RAII and generational-handle validation.
- Keep color-space decisions explicit at texture creation.
- Keep matrices, depth range, image origin, winding, and binding conventions documented at migration boundaries.
- Do not put legacy `Material`, `Texture`, or OpenGL calls into `BasicRenderer`.
- Avoid expanding `ViewportPanel`; move rendering ownership toward the render service.
- Preserve unrelated user changes and the current staging state.
