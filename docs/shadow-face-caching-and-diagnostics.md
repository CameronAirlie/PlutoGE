# Shadow face caching and diagnostics

## Plan

The 4155–4394 capture showed 125 point-atlas redraws in 240 frames, about
5.5 ms CPU per redraw, VSM preparation/recording costs of 1.36/1.98 ms, and
only nine glass copies saved. This phase targets unnecessary work and exposes
why the remaining work is required:

1. Independently validate point-light cube faces and report invalidation reasons.
2. Preserve atlas tiles with regional color/depth clears; share caster uploads.
3. Reuse immutable VSM preparation and avoid redundant opaque texture bindings.
4. Diagnose glass grouping, tighten conservative sampling bounds, and supply
   geometry-derived AABBs for rigid imported ranges without authored extents.
5. Separate continuous dirty-page demand from historical age, and report the
   triangle cost and budget deferrals by virtual-shadow level.

## Point shadows

`ShadowFaceCache` is a small RHI-independent publication policy. Each face key
contains projection, ordered caster membership/ranges, and content signatures.
A scene-wide signature provides a fast path before face-frustum classification.
Changed faces are cleared and recorded independently. The key is published only
after that face is recorded. Removal of the last caster still clears its face.
Unknown or displaced bounds retain conservative intersection behavior. Point
shadow comparisons also explicitly transform projected depth into the backend's
stored-depth range. The stronger opposite-face test exposed a pre-existing
OpenGL 4.3 fallback mismatch: without clip-control, zero-to-one projection depth
is stored as 0.5*z + 0.5. Comparing the unmapped value could miss nearby blockers.
The Vulkan/clip-control path uses the identity transform.

The profiler counts overlapping invalidation reasons: initial, projection,
caster set/range, and content. These describe cache-key changes, not a claim
that an individual change was caused by animation or LOD selection. Unchanged
faces remain valid across camera movement unless a graph depends on the view.

The RHI color-region clear complements the existing depth-region clear. Vulkan
uses attachment clears; OpenGL saves/restores scissor and color masks. Backends
without both operations refresh the atlas and invalidate any inactive face
whose contents were discarded. Cached object/material buffers are compared by
content and uploaded at most once per participating draw in a frame; all faces
share them. Recording caches mesh, pipeline, texture, and sampler bindings.

## VSM

Prepared chunks are reused only when every participating packet is immutable,
packet/mesh revisions and content signatures match, and projection-dependent
cluster planning is unchanged. Borrowed draw pointers are refreshed on reuse.
Camera parameters, pool changes, feedback, resolution recovery, and budget
changes still run through the existing frame policy. Mutable packets retain the
full preparation path. GPU decisions never depend on stale readback counters.

Individual indirect commands remain necessary for the current per-chunk uniform
layout. This phase reduces CPU preparation and unnecessary texture bindings; it
does not introduce multi-draw shader semantics or change geometry quality.

`oldestDirtyAge` now means continuously requested dirty age at scheduling time,
before this frame's updates. Dropping out of demand resets that age, and it is
also the age used for scheduling priority and oversized-page admission.
`historicalDirtyAge` preserves the original dirty start across unrequested
intervals. Neither diagnostic is a CPU/GPU synchronized wall-clock latency.

Per-level counters expose dirty and scheduled triangle cost. Dirty totals saturate
at the 32-bit counter limit rather than wrapping. Levels 0–3 are
fine directional, 4 is the directional root, 5–8 are fine spots, and 9–12 are
spot roots. Deferrals distinguish exhaustion of the page count from inability
to fit the remaining triangle allowance. Shared counter offsets scale with the
level count. No triangle/page budgets were silently raised.

## Glass

Footprint diagnostics distinguish unknown/deformed/nonfinite bounds and
near-plane fallbacks. Group boundaries distinguish conservative raster overlap,
sampling expansion, group-size limits, and intervening non-glass surfaces.
Copied pixel count exposes copy area as well as copy count.

For the shader's clamped IOR, Snell's law bounds the refracted normal component
by sqrt(1 - 1/IOR²). Sampling expansion therefore uses
`thickness * (10 + 1/max(0.1, sqrt(1 - 1/IOR²)))`, retaining the original
20*thickness bound at IOR 1. Raster order, blur guards, and unknown-bound
fallbacks remain conservative. Imported rigid ranges without authored extents
can use merged geometry-cluster bounds; no deformed/instanced extent is guessed.

## Validation and limits

Regression tests compare a partial point update with a full refresh, inspect an
opposite cached face after another face is cleared, check caster removal and
upload reuse, and exercise the existing cube-face/masked/graph tests. VSM tests
check immutable preparation reuse, invalidation, per-level triangle accounting,
and continuous/historical age ordering. Glass tests compare optimized copies
with full snapshots and check reported fallback/dependency reasons.

A matching Bistro capture is still required to measure real-scene gains.
Conservative bounds can still touch several faces or prevent glass grouping.
Point-shadow updates have no new temporal deferral; all dirty faces render in
the same frame. Per-face classification can add CPU cost on a global cache miss,
so its separate profiling scope should be retained when assessing the tradeoff.

Validation completed in Release on the RTX 4070 SUPER:

- Editor and runtime builds passed (`out/build/shadow-face-depth-build.log`).
- Full Vulkan and OpenGL suites passed, including visible opposite-face shadow
  preservation, full-refresh equivalence, graph/mask cases, and glass comparisons.
- Vulkan/OpenGL VSM-only tests passed, including pool resizing and strict versus
  oversized-page budgets.
- Vulkan VSM performance/preparation and counter-accounting checks passed.
- CPU clipmap/cache checks and `git diff --check` passed.

Detailed logs use the `out/build/shadow-face-*` prefix. Synthetic timing values
are not treated as a prediction of Bistro performance.
