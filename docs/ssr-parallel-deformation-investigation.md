# SSR ray marching and parallel deformation investigation

## Current evidence

The latest SSS capture averages 3.444 ms deformation/bounds and 0.391 ms
vertex upload for one 73,683-vertex update per frame. SSR averages 1.625 ms
trace and 0.194 ms resolve. Scene GPU is 13.401 ms and CPU frame duration,
including waits, is 14.361 ms. Improving CPU work need not improve FPS when
GPU completion remains limiting. Shadows remain closed.

## Parallel deformation: recommended first implementation

`RhiSceneRenderer.cpp` deforms a unique mesh/palette once per frame and shares
the result across submeshes and passes. With only one update in this capture,
parallelism across actors alone cannot help. Split the vertices of that mesh.

Each output vertex depends only on the corresponding input vertex, a read-only
joint palette and the corresponding previous output vertex. In-place history
is safe for disjoint ranges if the output vector is resized before dispatch
and no worker resizes or changes its ownership. Keep each vertex's arithmetic
and influence order unchanged, including the current SIMD path.

Proposed boundaries:

1. Extract a range kernel taking spans and returning raw minimum/maximum
   positions. Use it from both serial and parallel execution.
2. Use a bounded persistent executor, owned by the renderer or a reusable core
   task component with explicit shutdown. The caller participates in work.
   Start by benchmarking 1/2/4 total participants, not all logical processors.
3. Dispatch contiguous ranges only above a measured size threshold. Benchmark
   8K/16K/32K vertices and the SSS-sized 73,683 case; account for wakeup/wait cost.
4. Give each range its own bounds accumulator; merge raw min/max in fixed
   range order and derive the final centre/radius once. Do not merge spheres,
   which would change bounds and culling behaviour.
5. Finish all ranges before recording vertex upload, mutating cached pose/
   history state, or allowing frame/asset lifetimes to advance. All renderer
   cache changes and RHI calls remain on the rendering thread.
6. Drain outstanding work before cache invalidation or renderer destruction.
   Preserve serial fallback and the paused-pose/history-reset paths.

The source search found parallel import algorithms and a scene-baker helper,
but no reusable persistent frame-task executor in these paths. The baker
helper creates and joins threads per call; it is unsuitable to copy into the
per-frame skinning loop. Do not launch `std::async` per mesh/frame either.
The process reports 16 available logical processors; physical-core topology
was not established and is not a basis for choosing 16 workers.

Profiler requirement: `CpuTrace` is thread-local and the editor currently
attaches only the main thread. Worker scopes will otherwise be absent. Record
dispatch, caller work, wait and bounds merge separately, with per-worker
elapsed work as a separate statistic. Do not label summed worker wall times
as main-thread CPU execution or add them to frame duration.

Acceptance: compare serial/parallel positions, normals, tangents, bounds and
previous positions, including in-place history, zero/one/uneven vertex counts,
invalid influences, reflected and singular transforms, pause/reset, multiple
actors and cache invalidation. Then test rendering/motion and measure dispatch
through completion, including worker overhead. No parallel speedup is measured
yet; 3.444/4 is an arithmetic lower-bound illustration, not a forecast.

## SSR: cost and conservative skipping

Saved `Assets/PostProcessing/main.plutopostprocess` uses 48 march steps, five
binary refinements, distance 30, thickness 0.35 and start offset 0.08. Live
unsaved overrides remain possible: captures do not yet export these settings.
The shader has 16 fixed GGX samples. At 603 x 346 the default trace target is
302 x 173 (52,246 pixels). The ceiling is 40,124,928 march depth samples plus
4,179,680 refinement samples per frame, before pixel/ray rejection and early
termination. These are upper bounds, not measured texture-fetch counts.

The shader already rejects background/fully rough pixels, unsuitable ray
directions, offscreen rays and terminates at the first crossing. It reuses
the final crossing sample after refinement. Address arithmetic cleanup has
not materially improved measured SSR time. Target avoided march work next.

Proposed conservative block-skip experiment:

1. Build a separate R32F maximum-depth hierarchy from the current G-buffer,
   with explicit max reductions. Reverse Z makes the maximum the nearest
   surface. Ordinary averaged mipmaps cannot prove a region empty/in front.
2. Keep the existing discrete march schedule `start + distance * fraction^2`.
   Test small blocks of original sample indices; skip only when the hierarchy
   proves every depth query in the block would fail `behindSurface`.
3. SSR binds a linear, clamp sampler for depth. The hierarchy query must cover
   every texel contributing to the bilinear samples, including neighbouring
   cells, screen edges and odd dimensions. A centre-only cell test is unsafe.
4. Bound projected ray depth over the tested travel interval and use a strict,
   conservatively rounded comparison. With positive homogeneous W throughout,
   projected components are fractional-linear and endpoint bounds apply.
   Fall back to original marching at unsafe projection intervals, near ties,
   uncertain coverage or a failed proof. Preserve offscreen termination.
5. After skipping, set `previousTravel` to the last skipped original sample.
   Otherwise the next crossing's binary refinement bracket changes, even if
   the hierarchy proof was correct. Keep the existing first-crossing break,
   including when the thickness/normal test rejects the refined intersection.
6. Preserve all GGX directions, weights, sample counts, thickness rules and
   trace/resolve resolutions. Retain the original marcher for comparisons.

The RHI has storage mip bindings and shader barriers, with existing compute
mip-generation examples in VCT. That provides building blocks, not a finished
depth-hierarchy implementation. Isolate hierarchy ownership/building behind
a small render component; handle resize/reset and explicit producer/consumer
barriers on both backends. Verify supported formats and storage/sampling use.

Measure `hierarchy build + trace + resolve`, not just trace. At this small
viewport, hierarchy setup can consume the entire saving. Begin with an
optional diagnostic showing march/refinement counts, early-exit reasons and
proven skipped samples. Avoid always-on global per-fragment atomics. Add
captured configuration for steps, refinements and actual trace dimensions.

Acceptance: compare against the original marcher on thin occluders, depth
edges, grazing surfaces, sky, near-plane exits, odd sizes, 0/1/5/8 refinements,
and full/half resolution on Vulkan and OpenGL. Require image/hit equivalence
and lower total SSR cost before enabling. Native texture-stall/occupancy
counters were not measured during this source investigation.

## Execution order

1. Implement and benchmark bounded parallel skinning first: independence is
   clear and correctness can be checked directly against the serial kernel.
2. Add SSR configuration/work diagnostics, then test conservative block skips
   behind a reference comparison mode.
3. Request a new SSS capture only after correctness and benchmark gates pass.

This investigation changes documentation only; production rendering and
threading remain unchanged.
