# Bistro render preparation optimizations

## Baseline

The supplied Testing capture contains 240 frames (17444–17683), all at
1601 × 689, Vulkan normal rendering, VSync off, occlusion disabled and a
debugger attached. The capture does not identify the build configuration.

| Metric | Mean ms | p95 ms |
| --- | ---: | ---: |
| CPU frame | 18.134 | 26.688 |
| GPU scene observations | 6.082 | 8.070 |
| Command translation | 2.571 | 5.680 |
| Shadow recording CPU | 1.861 | 3.240 |
| Geometry recording CPU | 1.598 | 2.120 |
| Post-process recording CPU | 3.509 | 4.410 |

CPU frame means are 14.205 ms for 84 frames reusing completed VSM work and
20.251 ms for the other 156 frames. These groups also differ in packet
preparation and sorting: the difference is not solely shadow work.
GPU observations are delayed and parent scopes include their children.

## Changes

- Prepared visible, shadow and GI packets survive command reordering and
  removal. A bounded previous-list lookup uses reusable flat hash buckets;
  full source-value and material-revision comparisons resolve collisions.
  Mutable skinning and instance arrays remain uncached. Asset invalidation,
  transform/history changes and material edits still rebuild affected packets.
- Opaque batching preserves identities for unchanged individual packets.
  Actual merged/instanced aggregates receive fresh identities when built;
  callers without a revision allocator get uncached aggregates. This keeps
  downstream receiver/caster validation correct without invalidating every
  unmerged draw whenever another visible draw changes.
- Render sorting resolves shader, material, mesh and LOD range keys once per
  command, then sorts indices instead of repeatedly resolving resources and
  moving large render commands inside the comparator-driven sort.
- VSM recording caches pipeline and texture bindings within each pass.
  Vulkan mesh buffers are rebound only when the mesh changes. OpenGL retains
  mesh rebinding because pipeline changes select different VAOs. Caster input
  storage reserves the preceding frame's count. No delayed GPU result is used
  to skip newly changed shadow work.
- Consecutive glass panes with disjoint conservative sample footprints share
  a colour/depth snapshot. Overlap, unknown bounds and ordinary alpha-blend
  draws retain ordered dependencies. Planning compares against a growing
  union, making grouping conservative and linear. The profiler exports
  `RHI glass snapshots: N copies / M panes`.

No Testing scene assets, quality settings, shadow budgets or debugger settings
are changed. Occlusion remains a user setting; this work does not enable it.

## Validation and comparison

The MSVC Debug editor and regression executables are built under
`out/build/msvc-nvidia`. Graphics checks cover packet reordering/removal,
material/history/instance edits, mesh lifetime invalidation, glass image
equivalence and copy reduction, overlapping-pane fallback, opaque batching,
virtual shadows and temporal motion on Vulkan/OpenGL where supported.

All 12 selected checks passed: engine graphics integration; Vulkan preparation
cache; Vulkan/OpenGL opaque batching, transparency, VSM-only and temporal
motion; and both editor profiler tests. After the final flat-bucket cache
refinement, preparation/batching and profiler checks were rerun successfully.
`git diff --check` also passed.

The glass regression demonstrates two disjoint panes recorded with one copy,
versus two copies in the forced full-snapshot reference, with identical pixels.
The preparation regression verifies zero rebuilt packets after reordering
unchanged rigid commands and after removing one while retaining the others.
These are mechanism/correctness checks, not an editor FPS measurement.

For the capture comparison, retain the original build configuration and
debugger, Testing project, saved camera, viewport size, effects and VSync.
Warm resources before collecting 240 frames. Capture stationary and moving
camera runs separately; compare multiple equivalent runs rather than one
mixed-history FPS number. Keep normal rendering and occlusion mode unchanged.

```powershell
python tools/Compare-ProfilerCaptures.py before.txt after.txt
```

The tool reports mean/p95 timing deltas for all frames and for VSM reuse/active
groups separately, and flags differing captured conditions. Per-frame CPU
values have lower printed precision than the capture's summary. Build type,
camera path, hardware and debugger configuration still need manual matching.

A new debugger-attached editor capture is required to quantify gains against
the supplied baseline. Automated regression timings must not be substituted
for that comparison.

## Follow-up FPS pass

The next supplied capture contains 229 frames (39333 onward) with the same
reported debugger, resolution, diagnostic, occlusion and VSync settings. CPU
frames average 22.447 ms (p95 32.54 ms), versus 6.497 ms GPU observations.
The camera workload differs: geometry draw counts vary from roughly 213 to
561. Costs also rise in scene submission, event processing and presentation,
so the two mixed histories do not isolate the previous code changes.

This pass targets remaining draw-recording and batching costs:

- Material uniform preparation is shared across non-consecutive draws within
  a frame, with owned surface snapshots and full equality checks after hash
  lookup. Opaque and transparent variants remain separate. Frame-local
  ownership preserves edited values, graph time, fog and viewport changes.
- Glass snapshots invalidate only the sky texture binding that they overwrite;
  camera/material uniforms and unaffected textures remain bound. Entry into
  transparency invalidates the complete draw-state cache because intervening
  post-processing/GI passes can change state.
- Repeated pipeline, material, graph texture, emission texture and Vulkan mesh
  bindings are omitted when unchanged. OpenGL mesh buffers are still rebound
  for VAO correctness.
- Opaque batching uses contiguous bucket/group storage instead of allocating
  a map node and a vector for every singleton submesh.
- Captures now report prepared versus reused materials, allowing the expected
  reduction in repeated work to be checked in the actual editor view.

Regression additions exercise material hash collisions and edits as well as
copy sharing. The saved-scene benchmark is supplemental: it uses the existing
`--bistro-benchmark` harness (320 × 180, fixed camera, simplified GI/TAA/tone
stack, 100 warm-up frames). Its render-service timing is not total editor frame
time, and its Debug runs do not reproduce the attached editor debugger.

### Measured result

With the editor closed and no concurrent compilation, the previous executable
and updated executable each ran 340 frames, retaining the last 240 samples:

| Saved-scene harness stage | Previous ms | Updated ms |
| --- | ---: | ---: |
| Scene update excluding mesh submission | 1.573 | 1.658 |
| Mesh submission | 2.809 | 2.804 |
| Render service | 22.377 | 20.750 |

Render-service CPU time fell by 1.627 ms (7.27%). Both runs recorded an average
of 1,870 indexed and 265 non-indexed draws. The final 320 × 180 RGB captures
were byte-identical (zero changed channels). These are single before/after
run means, not a prediction of full editor FPS.

Logs and images are under `out/bistro-fps-before/quiet.log`,
`out/build/bistro-fps-after.log`, `out/bistro-fps-before/quiet/benchmark.ppm`
and `out/bistro-fps-after/capture/benchmark.ppm`.

All 13 selected graphics regression checks passed, covering Vulkan/OpenGL
render optimizations, VSM, transparency, temporal motion, opaque batching and
outlines, plus Vulkan preparation caching. An additional non-consecutive
material-reuse image test compares against forced separate preparation.

## Remaining CPU bottlenecks: visibility, submission and transparency

The latest attached editor capture averaged 16.114 ms CPU and 7.926 ms GPU at
1748 × 1137 with the debugger attached. Visibility preparation (2.059 ms),
mesh submission (1.949 ms) and transparency (1.846 ms) were the next targets.

- Submission now copies each command directly into its destination, skips
  ordering comparisons after the list is already dirty, and fast-paths
  byte-identical shadow transforms while preserving the tolerant comparison.
- LOD selection and transition evaluation share a metadata traversal. Updating
  only fade/transition data no longer dirties command ordering; changing the
  selected LOD still does. Invalid imported ranges retain their fallback behavior.
- Transparency projects each depth once, stably sorts indices, and moves draw
  packets once. Existing depth and graph-pass ordering are preserved.
- Glass footprints are computed once per frame. Sharing checks individual pane
  footprints instead of their enclosing union, avoiding false dependencies in
  empty gaps. Groups are capped at 64 panes to bound planning work. Overlapping
  panes and ordinary transparency still end a group; conservative bounds remain.

### Validation and measurements

The MSVC Debug editor and graphics test targets built successfully. All 14
selected tests passed: engine graphics API; Vulkan/OpenGL transparency, render
optimizations, VSM, temporal motion, opaque batching and outlines; and Vulkan
preparation caching. New checks compare LOD selection against the former
algorithm at thresholds and invalid ranges, and verify three-pane glass sharing
against forced separate snapshots with exact pixel equality.

Two alternating before/after runs used the saved Testing scene, fixed camera,
320 × 180 render target, 100 warm-up frames and 240 measured frames. Compilation,
the editor and other graphics tests were stopped during timings. Visibility was
measured separately with a viewport height of 1137, stationary and moving cameras,
40 warm-up iterations and 240 samples per camera. Submission is outside those
visibility timers.

| CPU stage | Before 1 ms | After 1 ms | Before 2 ms | After 2 ms |
| --- | ---: | ---: | ---: | ---: |
| Scene update excluding submission | 1.618 | 1.463 | 1.112 | 1.142 |
| Mesh submission | 2.760 | 1.479 | 2.151 | 1.253 |
| Render service | 20.256 | 16.557 | 16.505 | 14.446 |
| Stationary visibility | 0.749 | 0.606 | 0.694 | 0.577 |
| Moving visibility | 0.720 | 0.734 | 0.669 | 0.562 |

Submission improved 42–46% and render service 12–18% in these pairs. Stationary
visibility improved 17–19%; moving visibility was mixed (2% slower, then 16%
faster). Run-to-run variation is substantial, so these timings are directional,
not a guaranteed FPS increase. The harness is Debug but has no attached debugger
and uses a simplified post-processing stack; the actual editor needs a fresh
capture at the same camera, viewport and debugger configuration.

All four final RGB images have SHA-256
`7B7F2D744CFA3BBB53701E703D9FD337D4DE77896C44994143CF374890D99456`.
Each run averaged 1,870 indexed and 265 non-indexed draws. Visibility checksum
values are order-sensitive and differ even between unchanged baseline launches
because command keys include pointer addresses; they are not cross-run correctness
evidence. LOD regression checks and image comparisons provide that validation.

Evidence: `out/bistro-remaining-before/{run,repeat}.log`,
`out/build/bistro-remaining-after.log`, `out/bistro-remaining-after-repeat.log`,
`out/bistro-remaining-{before,after}/{capture,repeat}/benchmark.ppm`,
`out/build/bistro-remaining-build.log` and
`out/build/bistro-remaining-regressions.log`.
