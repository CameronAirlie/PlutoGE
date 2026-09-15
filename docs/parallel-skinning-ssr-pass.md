# Parallel skinning and SSR performance pass

## Plan

1. Split the existing SIMD deformation kernel into disjoint vertex ranges.
2. Add a bounded persistent executor, preserving serial fallback, history,
   exact min/max bounds and render-thread upload ownership.
3. Validate serial/parallel output, lifecycle and scheduling threshold cases;
   measure complete calls with two and four participants.
4. Prototype conservative SSR block rejection without a hierarchy allocation,
   compare complete images, and measure whether fewer samples offset checks.
5. Keep only supported performance changes enabled; expose useful capture data.

## Parallel deformation implementation

`RhiSkinningExecutor` owns up to three sleeping workers; the calling thread
processes one range. A renderer creates it lazily for a mesh of at least 32,768
vertices and reuses it. Smaller meshes call the serial kernel. The participant
count is capped by hardware concurrency and four total participants.

The output vector is resized before dispatch. Source vertices and joint
matrices are read-only for the synchronous call. Previous vertices may alias
output: each range reads and overwrites only its own indices. Each range uses
the unchanged per-vertex SIMD arithmetic and returns raw min/max; merging
those produces the same final centre/radius. No GPU API, renderer cache or
palette/history mutation occurs on workers.

Dispatch uses a generation and condition variables. The caller waits for all
workers before returning, so asset invalidation cannot race outstanding work.
Shutdown joins the persistent workers; partial thread-creation failure also
joins already-created workers before propagating the error. The executor is
single-caller, matching renderer ownership.

Copied captures add participant count plus dispatch, caller-work, wait and
bounds-merge wall times. They are parts of deformation time, not additional
cost to add to it; worker CPU execution is not inferred from main-thread traces.

## SSR experiment

The tested opt-in prototype checked blocks of four existing march samples. If both
projected endpoints stay safely within the same bilinear cell, it gathers
the four raw depths and uses their maximum as a conservative bound. A strict
depth margin rejects the block only if all its samples are in front. Uncertain
projection/cell-boundary cases retain the original marcher. The last skipped
sample becomes `previousTravel`, preserving the first-hit refinement bracket.
No ray count, quality, thickness, GGX or resolve setting changes.

This was a smaller experiment than a depth pyramid: it avoided the hierarchy
build cost, but its checks cost more than the samples they saved. It has been
removed from the production shader and test harness following measurements.

Captures also report the first SSR effect's configured march/refinement counts
and trace dimensions. These are not measured sample counts or early-exit rates.

## Validation

Build log: `out/build/parallel-ssr-build.log`.
Skinning compares every vertex field and bounds at sizes 0, 1, 32767, 32768,
32801 and 73683 with changing palettes and in-place previous data, using two
and four participants. Existing skinning tests retain independent reference,
invalid influence, reflection, motion, bounds and GPU upload coverage.
SSR compares all RGBA8 pixels with skipping on/off at 0/1/5/8 refinements,
full/half resolution, and odd/even sizes on Vulkan and OpenGL.

## Final results

The final RelWithDebInfo editor and rendering tests built successfully.
Eight final checks passed: Vulkan skinning, Vulkan/OpenGL SSR, RHI and temporal
motion, plus editor profiler capture tests. The skinning check additionally
exercises the renderer-owned executor with two independent large actors,
animation, cache invalidation and shutdown.

Matched CPU benchmark, 73,683 vertices and four influences, milliseconds per
complete synchronous deformation (including dispatch, waiting and bounds):

| Participants | Run 1 | Run 2 | Run 3 | Mean |
| --- | ---: | ---: | ---: | ---: |
| Serial | 3.65532 | 3.71297 | 3.65875 | 3.67568 |
| 2 | 1.88536 | 1.90968 | 1.92277 | 1.90594 |
| 4 | 0.976298 | 0.952202 | 1.03041 | 0.98630 |

Four participants reduce elapsed deformation by about 73% (3.7x) in this
synthetic workload. Keep this path enabled for large meshes. The 32K threshold
is conservative rather than auto-tuned; smaller jobs keep serial execution.
Actual SSS latency and FPS still require a project capture with its workload.

SSR experiment: image comparisons between its on/off modes passed, but trace
cost rose from 35.055 to 68.758 ms at full resolution and from 9.438 to
18.047 ms at half resolution. Even its disabled mode cost more than the
original shader. Remove the branch entirely rather than ship a disabled path
that adds shader complexity. Archived experiment:
`out/build/SSR-block-skip-rejected.slang` and
`out/build/SsrBlockSkipChecks-rejected.h`; measurements:
`out/build/ssr-block-benchmark.log`.

After removal, SSR trace returned to 30.432 ms full resolution and 8.221 ms
half resolution in the stress benchmark. All 93,380,352 saved RGBA8 bytes
match the pre-experiment baseline. No SSR performance improvement is claimed.
A future hierarchy experiment must amortise checks across larger regions and
include hierarchy build cost; same-cell four-sample checks are not promising.

Final logs: `out/build/parallel-final-build.log`,
`out/build/parallel-final-tests.log`, `out/build/ssr-restored.log`.
The retained SSR change in this pass is captured configuration diagnostics.

## Next SSS capture

Restart the rebuilt editor with the debugger attached, retain the same view
and resolution, and capture normal rendering. Compare deformation/bounds
and the new dispatch/caller/wait/merge fields separately from upload. GPU
pressure may replace saved CPU time with presentation waiting, so a 3.7x
kernel result does not imply a corresponding FPS increase. Shadows remain
closed and no further shadow-specific capture is required.
