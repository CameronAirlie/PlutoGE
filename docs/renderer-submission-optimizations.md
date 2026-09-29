# Renderer submission follow-up

The uncapped capture at frames 254483–254722 has no persistent uniform uploads
or packet rebuilds. Its remaining geometry recording cost is 1.231 ms, including
0.613 ms coverage recording; descriptor preparation averages 0.557 ms and overlaps
recording. These numbers identify CPU preparation work, not independent savings
that can be added together.

## Changes

- Opaque coverage uses dedicated jittered depth shaders with camera/object
  bindings only. It bypasses material preparation and texture binding. Standard
  masked coverage retains material constants and base alpha. Graph coverage
  retains the full layout and exact graph evaluation. Shader packages without the
  optional depth variants use the existing fallback path.
- Material preparation owns immutable GPU records alongside CPU surface snapshots.
  Hash matches require surface equality. Viewport changes invalidate records;
  glass fog changes invalidate transparent records. Time-dependent graphs use
  frame-local transient buffers with within-frame sharing. Age-based eviction and
  an 8 MiB soft budget bound inactive records; active working sets may exceed it.
  The existing object parameter cache remains separate, so its eviction cannot
  invalidate material handles. Shutdown releases both caches.
- Vulkan marks descriptor sets dirty by typed binding slot. Preparing a changed
  material set reuses unaffected camera/object sets, while dynamic uniform offsets
  still update on every required draw. Pipeline changes, command buffer resets,
  and external commands invalidate all sets. Empty descriptor writes are skipped.
- Imported skeleton/node metadata no longer forces per-frame mesh submission
  without an animation owner. Attaching/removing an ancestor animator queues the
  affected subtree, including multiple mesh components. Reparenting retains its
  existing subtree invalidation. Meshes with an animator deliberately remain
  dynamic: playback state alone cannot detect scrubbing, layers or ragdoll poses.
- Packet list placement uses producer identity when available. Legacy commands
  without IDs also validate their model before taking the unchanged-order path.
  Repeated mesh/material pairs alone cannot identify a list position. Reordering
  now reconciles matching cached packets instead of unnecessarily rebuilding them.

Object motion history, skinning, graph time, camera visibility and removal retain
their existing update paths. No post-process behavior or quality settings change.

## Validation and next capture

The render optimization regression checks warm material preparations, persistent
uploads, deliberately colliding material keys, edited pixels and viewport changes.
Its reference image comparisons include masked, instanced and mirrored geometry.
The overdraw workload reports CPU recording, descriptor time, bind count and
material preparations alongside GPU geometry time. Timing values are diagnostic;
image correctness and cache behavior are assertions.

The glass regression now checks cross-frame reuse and exactly one preparation
for one edited pane. Its earlier per-frame preparation counts described the
superseded frame-local cache. Reorder checks independently verify unchanged
pixels and zero packet rebuilds.

For the actual scene, capture at the same camera, viewport, render scale and
lighting after warm-up. Check material preparations (zero for unchanged,
time-independent materials), coverage CPU recording, descriptor preparation,
unchanged producer leases, scene active CPU and total scene GPU time. Descriptor
preparation is nested within recording. Presentation waits and editor UI remain
separate costs.

Use the existing geometry **Automatic sweep** to investigate opaque shading.
Each mode runs for 32 frames and GPU scope names preserve the producing mode
across asynchronous readback. Hold the camera fixed, warm a full cycle, and retain
at least one further 224-frame cycle. Compare normal geometry with minimal
shading, material-only, and the shadow/texture/light bypasses. Cached VSM generation
does not eliminate VSM sampling in opaque shading.

Test occlusion **Measure** then **Cull** on the same view, comparing total geometry
and culling costs and checking visibility while moving the camera. Leave occlusion
off when the extra work exceeds the saved shading. This capture alone does not
justify changing its default or rewriting the lighting architecture.

## Completed validation

The RelWithDebInfo editor and both graphics test executables build successfully.
All 23 selected checks have passing results across the validation runs: eight
OpenGL, thirteen Vulkan and two CPU/lifecycle checks. The initial glass count
failures are superseded by the cross-frame assertions; the packet-reorder failure
is superseded by the identity-aware reconciliation fix. The affected preparation,
skinning, batching and CPU checks passed again after that fix.

The stationary regression reports zero material preparations and zero persistent
uniform uploads after warm-up (2,544 persistent bytes on cold first use). Material
edits change pixels, viewport changes invalidate constants, and time-dependent
graphs remain dynamic. These verify mechanisms, not a measured speedup in the
user's scene.

Evidence:

- `out/renderer-next-reconcile-build.log` — final editor/test build.
- `out/renderer-next-regressions.log` — broad run, including the superseded failures.
- `out/renderer-next-glass-regressions.log` — passing full renderer and transparency checks on both backends.
- `out/renderer-next-reconcile-tests.log` — passing final preparation, skinning, batching and CPU checks.
