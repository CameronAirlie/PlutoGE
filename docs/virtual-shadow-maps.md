# Virtual shadow maps: feasibility and proposed architecture

Status: investigation, not implemented. September 2026.

## Recommendation

Keep corrected cascaded shadows as the production path. Prototype a directional-light virtual shadow map behind a rendering option, using a bounded physical depth atlas and a coarse cascade fallback. Do not introduce Vulkan sparse-memory requirements for the first version: software page-table addressing can work on both existing backends.

Virtual shadow maps improve where resolution is allocated; they do not remove the need for comparison filtering, receiver bias, or handling grazing light angles.

## Existing foundations and gaps

- `RhiSceneRenderer.cpp` computes directional projections; `BasicRenderer.cpp` owns RHI shadow targets, draw lists, caching and lighting bindings. Put backend-independent page residency and clipmap policy in a separate shadow resource manager rather than expanding these classes with an entire allocator.
- `rhi/RenderDevice.h` exposes compute dispatch, storage images, shader barriers, viewport/scissor controls and instanced indexed draws. Storage-image-based request masks and page tables are possible with this interface.
- The command interface has no general storage-buffer binding, indexed indirect drawing or indirect-count dispatch/drawing. Production GPU page compaction and caster binning need explicit capabilities and implementations in both device backends. Some existing compute methods default to no-ops; virtual shadows must check capabilities and fail over rather than silently assume support.
- `ShadowPass.cpp` already tracks old/new caster bounds, skeletal motion, dirty regions and cascade scrolling. Reuse the invalidation concepts, not its OpenGL-specific resource operations. Cache keys must also cover topology, alpha-mask/material changes, LOD changes and animated vertex deformation.
- RHI opaque shading currently generates depth while sampling shadows. Depth-driven page requests therefore require a receiver depth prepass (or previous-frame requests with a robust current-frame fallback). Transparent, volumetric and off-screen GI receivers also need explicit coverage or cascade fallback.

## Proposed frame sequence

1. Generate receiver depth, reconstruct visible world positions and select clipmap levels from projected pixel footprints.
2. Mark required virtual pages, including the filter footprint across page boundaries.
3. Retain valid resident pages; allocate missing pages from a fixed-size physical pool, evicting unused pages under a documented budget. Track generation IDs to prevent stale mappings.
4. Invalidate overlapping pages using both previous and current caster bounds. A sun-direction change invalidates the light's cache.
5. Bin casters into dirty pages and render only those pages. Preserve valid atlas tiles: attachment clears must not wipe cached pages; use a scoped page clear or a dedicated depth-clear draw. A CPU-binned prototype is acceptable for feasibility, but CPU readback in the steady-state frame loop is not the production design.
6. Publish valid mappings only after rendering and required resource synchronization. Translate every comparison tap through the page table so filtering never reads an unrelated physical neighbor. Missing pages sample a coarser resident level or the fallback cascade.

## Initial budgets and evaluation

An illustrative 4096-square D32 atlas uses 64 MiB before page tables, request masks, fallback maps and driver overhead. With 128-square pages it holds 1024 tiles, excluding any guard borders. A virtual 16K address space does not require a fully allocated 16K physical texture.

Instrument requested/resident/dirty/evicted pages, overflow, cache-hit rate, caster-page pairs, submitted triangles, GPU timings and total memory. Compare against corrected cascades at equal GPU time and memory on both Vulkan and OpenGL. Include stationary scenes, camera cuts, sprint/FOV changes, a moving sun, skeletal characters, foliage alpha masks, disocclusion and terrain spanning many pages. Validate seams and missing-page behavior before adding softer-shadow algorithms.

The principal performance risk is repeatedly drawing large meshes into many pages. Existing mesh/submesh LODs and instancing help, but page-level geometry culling is a separate project. Start with one directional light; defer point-light faces and spotlights until cache and submission costs are measured.

## Reference and limits of the comparison

[Epic: Virtual Shadow Maps](https://dev.epicgames.com/documentation/en-us/unreal-engine/virtual-shadow-maps-in-unreal-engine) describes 16K virtual maps split into 128-square pages, requested from screen depth and cached across frames. Directional lights use clipmaps. Epic also documents invalidation from moving lights/geometry and the importance of Nanite for its implementation's performance. PlutoGE does not inherit those geometry-submission advantages merely by adding page tables; the architecture above is a proposal based on this repository, not a claim of Unreal-equivalent performance.

## Related cascade correction

Both lit paths now filter consecutive shadow texels with a separable tent, pairing adjacent weights into bilinear comparisons. The RHI path retains receiver-plane correction at each texel centre. Screen-space blur radius no longer spreads the RHI shadow-map samples apart. This removes the coverage plateaus left by the first bilinear-only correction. Both projection builders fit actual camera FOV; the legacy XY padding is reduced to a filter-sized guard plus a small world-space margin. Sphere quantization and world-anchored snapping remain.

Tradeoffs: default softness uses at most 36 depth reads per cascade in both paths. Maximum softness (4) uses at most 100; zero softness reduces to four. This costs more than the original sparse filters, but evaluates the full footprint rather than leaving gaps. Actual-FOV fitting improves density but changes texel scale during FOV animation. Validate aiming/sprinting in the target scene. A future explicit stable-FOV envelope could trade density for FOV-animation stability without imposing a universal 90-degree minimum.

## Validation of the cascade correction

- Built `PlutoGERhiShaders`, `PlutoGERender`, `PlutoGEVulkanRhiTests` and `PlutoGEOpenGLRhiTests` in RelWithDebInfo. Slang generated both GLSL and SPIR-V successfully.
- Full Vulkan RHI suite passed. The new shared GPU regression test reported 252 distinct intermediate coverage levels in a magnified coarse shadow edge.
- Negative controls: the original binary PCF produced eight intermediate levels and failed the initial coverage check. The first bilinear-grid correction fails the new screen-space-radius independence check. The current tent filter passes both coverage checks and the straight-edge plateau check (longest intermediate plateau: one pixel). Fixed shaders were restored after each control.
- OpenGL runtime validation remains incomplete: CTest exited with 0xc0000409 before reporting test output; an unsandboxed focused retry returned 1, the window-creation failure path. No OpenGL runtime pass is claimed.
- The screenshot's exact scene and animated-FOV behavior have not been visually revalidated. The GPU check isolates shadow filtering and does not measure scene performance or cascade fitting.

Focused reproduction: `build/tests/RelWithDebInfo/PlutoGEVulkanRhiTests.exe --shadows-only` (or the corresponding OpenGL executable). The shared test also runs as part of each existing RHI suite.

### Scene-specific follow-up

Read-only inspection of `D:/PlutoProjects/SSS/Assets/Scenes/Main.plutoscene` found resolution 2048, four cascades, resolution falloff 0.75, near distance 8, softness 1, and screen-space radius 4. The previous RHI shader used the maximum of softness and screen-space radius as shadow-map tap spacing, so this scene sampled at four-texel intervals. The revised filter uses only shadow softness for its footprint. A GPU regression verifies that enabling the screen-space radius-four setting leaves RHI shadow-map coverage unchanged. The scene file was not modified.
