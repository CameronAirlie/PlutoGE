# Virtual shadow maps

Status: experimental GPU-driven directional VSM implementation. September 2026.

## Selecting the shadow method

On a directional light, select **Shadow Method: Cascaded Shadow Maps** or **Virtual Shadow Maps (Experimental)**. Existing scenes default to cascades. The choice and update budgets are serialized as component properties and shared between editor and runtime through `scene::ApplyDirectionalShadowSettings`. Direct RHI callers use `BasicLighting::shadowMethod`.

The RHI implementation supports Vulkan and OpenGL through explicit storage-buffer, compute, indexed-indirect and shader-clip-distance capabilities. Unsupported devices use cascades. The legacy OpenGL render-pass pipeline retains its existing cascade implementation. Point and spot lights are unchanged.

## Architecture and frame sequence

`VirtualShadowMaps` owns clipmap policy, GPU resources and VSM pass recording. `BasicRenderer` supplies immutable mesh revisions, draw signatures and a mesh-submission callback. Scene properties are translated separately from renderer implementation. Shared constants and data layouts live in `VirtualShadowConfig.h` and `VirtualShadowCommon.slang`.

1. Render current-frame receiver depth, including alpha-masked receivers. Reconstruct visible world positions in compute and select levels using pixel footprints. Atomically mark and compact unique page requests, including the shadow filter guard.
2. Use four world-stable directional clipmaps, each representing a 16,384-square virtual map with 128-square pages. Absolute light-space page coordinates preserve cached content while the camera scrolls. Light rotation, scale or snapped depth-range changes alter the projection epoch and invalidate affected content.
3. Retain requested resident pages in a fixed 256-page pool. Each level receives a fair share; unused capacity is lent to other levels. Allocate from unrequested slots without a CPU feedback round trip.
4. Compute page content signatures from intersecting caster chunks. Signatures cover mesh revisions, transforms/instances, submesh ranges, bounds and alpha material inputs. Caster movement, removal and changed materials invalidate affected content. Unknown bounds conservatively intersect every page.
5. Select dirty updates under both page and triangle budgets. Rotate update priority across the pool. Generate caster/page lists and indexed-indirect arguments on the GPU. Clear only dirty physical tiles with a generated depth-clear draw, then instance each caster chunk across its selected pages. Hardware clip distances confine geometry to each physical tile.
6. Publish mappings after rendering and resource barriers. Lighting checks that the complete filter footprint is resident before sampling it. Missing fine pages try coarser resident levels, then use the conventional cascade filter. This avoids repeatedly evaluating cascade filtering inside every missing VSM tap.

Transparent receivers can sample resident pages and otherwise fall back to cascades. GI and volumetric shadow sampling retain conventional cascade coverage. Persistent maps and asynchronous diagnostics are independent: no CPU readback determines allocation or submission.

## Performance controls and instrumentation

Directional-light properties expose **VSM Page Updates per Frame** (default 64, range 1–256) and **VSM Triangle Budget per Frame** (default 1,000,000, range 1–16,000,000). These limit VSM atlas updates; receiver depth, fallback cascades and ordinary scene rendering are additional work. Pages too expensive to update within the budget remain on fallback instead of publishing stale depth. Large meshes should have accurate bounds and useful submesh or LOD granularity.

There is one indexed-indirect command per caster chunk, rather than one CPU command per caster/page pair. Each chunk contains at most 64 instances; GPU page instancing still incurs actual geometry work, which the triangle budget bounds. The implementation supports up to 4096 chunks and falls back to cascades above this limit.

The physical D32 atlas and companion R32 attachment occupy 32 MiB in total. Request masks and page tables add approximately 512 KiB. Caster lists, indirect arguments, receiver targets, uniforms and fallback maps are additional; the profiler reports allocated VSM resource estimates. Disabling VSM releases its resources.

GPU scopes distinguish receiver depth, GPU planning and atlas rendering. The profiler reports current CPU submission counts separately from delayed GPU statistics: requested/resident/dirty/updated/deferred pages, hits, evictions, overflow, nonempty indirect draws, caster/page pairs and triangles. Diagnostic readbacks reuse staging buffers and existing frame completion; unavailable readback slots skip a sample without adding a wait. Vulkan post-processing now exposes individual effect timings as well as inclusive parent scopes. Do not sum parent and child scopes.

### Supplied performance capture

The supplied 582×507 capture reported 39.17 ms average frame time, 47.33 ms GPU frame time, 24.53 ms GPU VSM time, 7422 caster/page pairs, 21,887,083 VSM triangles, 7883 shadow draw calls, 256 dirty pages and zero cache hits. This motivated depth-based requests, stable clipmaps, GPU indirect submission, bounded updates and cheaper missing-page filtering. The same capture reported 19.96 ms post-processing GPU time, so optimizing VSM alone cannot establish an overall frame-time target. Presentation fence waits reflect outstanding GPU work and should not be treated as a separate additive rendering cost.

These changes are not a measured before/after result for that scene. Use the same camera, content, debugger state, resolution and effects when comparing captures. Per-effect timings now make the post-processing contribution easier to isolate.

The synthetic 120-caster Vulkan workload on AMD Radeon(TM) Graphics measured:

| Workload | GPU frame | VSM planning | VSM atlas rendering | VSM triangles/frame | Total indexed commands/frame |
| --- | ---: | ---: | ---: | ---: | ---: |
| Stationary | 1.60 ms | 0.72 ms | 0.008 ms | 0 | 122 |
| Camera movement | 2.80 ms | 0.73 ms | 0.008 ms | 0 | 602 |
| Animated caster | 3.61 ms | 0.75 ms | 0.79 ms | 768,000 | 602 |

Values average 15 samples after warm-up per scenario. Stationary and camera-motion samples retained 64 cache hits; the intentionally unbounded animated caster invalidated all requested pages, with 62 deferred updates using cascade fallback. These results demonstrate cache reuse and bounded work, not target-scene shadow coverage or final performance. GPU timings vary by load and hardware.

## Validation

### Performance follow-up after scene validation

The next supplied capture reported 25.47 ms average frame time (previously 39.17 ms), 22.94 ms scene GPU time, 72 requested/resident/cached VSM pages and no dirty pages or VSM triangles. VSM atlas rendering was 0.01 ms. The user confirmed acceptable visuals and deferred camera-motion shadow glitches; this follow-up targets performance.

Post-processing remained the dominant scope at 21.82 ms. Individual child scopes originally used TOP-to-BOTTOM timestamps, which could include unfinished preceding passes. They now use completion timestamps at both boundaries; parent scopes remain inclusive and should not be added to their children. These timings describe completion intervals, not isolated execution times or guaranteed additive savings.

SSR now transforms the ray origin once per pixel and directions once per ray, reusing their affine form through marching and binary refinement. Depth intersection reconstructs only homogeneous W and view Z. The 16-ray estimator, configured march/refinement counts, material response and hit criteria are unchanged. `PlutoGEVulkanRhiTests --ssr-performance` measured 17.63 ms before and 11.89 ms after for reflections at 582×507 on AMD Radeon(TM) Graphics, averaging 32 frames after warm-up (about 33% lower). This is a controlled reflection workload, not a predicted improvement for the user's scene.

Diffuse VCT GI now skips cone tracing for fully metallic receivers, whose contribution is zero, and computes the shared cone origin/voxel size once per pixel. Debug views retain their existing behavior. The full Vulkan rendering suite and focused VCT cache tests passed, including rough/dielectric/metallic SSR checks and local-bounce/cache regressions.

### Reproduction

The subsequent scene capture reported 22.74 ms average frame time, 21.31 ms scene GPU time, 7.37 ms SSR and 5.61 ms geometry. All 78 requested VSM pages were cached, with no updates. A further SSR optimization compares reverse-Z depth directly during marching and refinement, retaining view-space reconstruction for the final thickness test. The same controlled benchmark decreased from 11.89 to 9.33 ms (about 22%). All nine RGBA8 snapshots from smooth, rough and dielectric checks were byte-identical to the previous shader. Optional snapshot output is available with `--ssr-performance <output.rgba>`; it contains nine consecutive 582×507 RGBA8 images. The visible workload differed between scene captures (40 draws versus 45 previously), so their frame times are observational rather than a controlled benchmark.

- `PlutoGEVirtualShadowClipmapTests` checks world-coordinate reuse under sub-page motion and page scrolling, and projection epoch changes under light rotation.
- Shared GPU shadow checks exercise depth requests, residency, stationary cache reuse, scrolling, movement/removal, alpha masks, overflow and budget fallback, and runtime switching. Run either RHI executable with `--shadows-only`.
- `PlutoGEVulkanRhiTests --vsm-performance` runs a synthetic 120-caster workload at 582×507 with deliberately unknown bounds. It checks stationary convergence and bounded page updates, triangle counts and CPU indexed submissions during camera and caster movement. It is a regression workload, not a reproduction of the supplied scene.
- `PlutoGEVirtualShadowMapCacheTests` retains coverage of the standalone CPU reference allocator; the renderer uses GPU residency rather than that allocator.

RelWithDebInfo builds passed for the editor, runtime and both RHI test executables; Slang compiled the VSM shaders for both APIs. The full Vulkan suite and all three focused VSM CTest cases passed on AMD Radeon(TM) Graphics. The Vulkan suite also checks nested GPU timing under query-budget exhaustion. OpenGL's focused executable still exits during window creation, so no OpenGL runtime pass is claimed.

## Remaining limits

VSM remains experimental and opt-in. Fallback cascades stay allocated and maintained, so VSM adds work and memory and does not guarantee a speedup. Geometry is culled at caster-chunk bounds, not at meshlet or triangle level; this implementation does not provide Nanite-style geometry virtualization. The fixed physical pool can overflow, and deliberately restrictive budgets can retain coarse fallback for expensive pages. More physical capacity, geometry clustering and additional light types are separate extensions, not prerequisites for the implemented directional path.

OpenGL runtime acceptance and target-scene visual/performance acceptance must be completed on a working graphics context. No production default or Unreal-equivalent performance is claimed.

## Reference

[Epic: Virtual Shadow Maps](https://dev.epicgames.com/documentation/en-us/unreal-engine/virtual-shadow-maps-in-unreal-engine) describes screen-depth requests, 16K virtual maps, 128-square pages, directional clipmaps and cache invalidation. PlutoGE uses software page-table addressing and requires no sparse-memory support. VSM redistributes shadow resolution; it still needs filtering, receiver bias and handling of grazing light angles.

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
