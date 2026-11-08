# Retained packets and incremental glass snapshots

The subsequent frontend migration is described in
[Retained render scene](retained-render-scene.md).

## Capture and scope

The 240-frame capture 9986–10225 averages 17.508 ms CPU and 10.842 ms scene GPU.
VSM recording is down to 0.283 ms CPU; preparation remains 1.565 ms. Transparency
costs 2.576 ms CPU and 4.963 ms GPU, with 155.76 color/depth copies and 184.48 million
copied pixels per frame. Scene submission, visibility preparation and the three
packet-preparation scopes remain substantial. Workloads differ between captures;
these figures identify priorities, not a controlled speedup measurement.

This phase implements a retained rigid-packet layer and a damage-tracked snapshot
dependency model. It is the first migration step toward a retained render scene;
the frontend still submits command lists and visibility/LOD selection still scans
them. It does not claim to replace those systems with a spatial scene database.

## Retained object preparation

Rigid MeshComponent producers have process-local identities that do not depend on
memory addresses. Copying a producer creates a new identity; cached commands from
a clone are rebuilt before use. Source revisions cover object transforms, bounds
and previous-frame history. Camera-selected LOD, material revisions, geometry
identity and caster eligibility remain independently validated by the consumer.

RhiDrawPreparationCache shares prepared packets between visible, shadow and GI
lists. List-local hits remain inexpensive; a list miss can recover a retained
packet instead of rebuilding it. Geometry variants, materials and emissive GI's
LOD override have distinct keys. Reused packets retain the same downstream
preparation token. Producer IDs and revisions are optional: procedural commands,
mutable instance arrays and skinned poses retain the existing validated path.

The shared store has a 16,384-entry admission limit and retires entries unused for
more than 120 frames during periodic maintenance. Active lists own their packets,
so eviction only affects reuse opportunities. Explicit asset invalidation and
expired mesh lifetimes reset retained packets with the existing asset caches.
IDs/revisions are transient and are not serialized scene identifiers.

Missing authored submesh bounds are now derived once per uploaded mesh/index
range, rather than repeatedly merging the same geometry clusters for different
objects and passes. Frontend submission sorting reuses scratch allocations for
identities, reordered commands and caster flags.

Future work: replace duplicated list payloads with handles into a retained scene,
add producer change queues and spatial visibility candidates, then migrate instance
and animation data with explicit revisions. This phase deliberately preserves
their mutable-data correctness contract.

## Glass dependency model

Opaque depth is immutable throughout ordinary transparency because translucent
pipelines disable depth writes. Capture it once. Shader graphs retain their
independent opaque color snapshot; glass retains exact back-to-front color reads.
Particles keep their existing separate snapshot behavior.

A reusable glass color snapshot starts with every 32x32 screen tile invalid.
Before a glass group reads it, copy only invalid tiles intersecting the group's
conservative sample footprint. After every transparent draw, mark tiles touched
by its conservative raster bounds dirty, including ordinary alpha blending and
shader-graph overlays. Unknown/deformed bounds dirty the whole viewport.

SnapshotDamageTracker owns only dependency state. Planning does not publish clean
tiles: the caller commits complete tiles after recording their copies. Adjacent
tile rows merge into rectangles. More than eight disjoint rectangles collapse to
a conservative bounding rectangle to limit command overhead. Tiles at viewport
edges are clipped to the attachment extent. State resets every transparency pass,
so no previous-frame camera or object state can leak into snapshot validity.

This retains the exact layered refraction model. It changes neither material
quality nor render resolution. A large sampling footprint no longer implies that
all of its pixels need recopying after a small pane writes to the scene. Truly
full-screen writes still require large refreshes.

## Diagnostics and validation

New profiler counters report shared retained-packet hits, immutable depth copies,
and clean color-snapshot reuse. Existing copied-pixel and copy-count counters now
measure actual color refresh rectangles, which may differ from glass group count.

Tests compare bounded incremental snapshots with full-copy references, including
layered large-read/small-write panes. Planner checks cover tile validity and
read/write boundaries. Retained-packet checks cover cross-pass reuse, removal and
return to visibility, producer revision changes, cloned identities, asset reset
and mutable instance fallback. Existing image suites cover both Vulkan/OpenGL,
shader graphs, shadow filtering, glass grouping and refraction.

Validation completed in Release: editor/runtime builds, the full Vulkan and
OpenGL RHI suites, SceneCpuCacheTests and VulkanVsmPerformanceTests passed.
On both backends, the 12-pane layered-glass case copied 110,848 color pixels
instead of 691,200 (84% less), with exact image equality and one depth snapshot.
This measures copy volume in the regression scene, not total rendering speed.

For the next real-scene capture, keep the camera path, scene state and viewport
resolution fixed. Compare glass copied pixels and GPU time, shared packet hits,
packet preparation CPU, VSM preparation CPU and total CPU frame time. Reduced
copy traffic and synthetic test results are not themselves a full-scene FPS claim.

## Transparency CPU regression follow-up

Capture 16698–16937 reduced glass color refreshes to 2.29 million pixels/frame,
but transparency CPU self time increased to 19.156 ms and total CPU to 33.844 ms.
Transparency GPU fell to 3.129 ms. The capture does not identify the individual
CPU operations; an attached debugger alone does not establish build configuration.

The tile tracker now stores 64-bit words per row, skipping clean spans and updating
write/copy spans with masks. Set-bit runs merge across word boundaries before
forming rectangles. Each returned plan has fixed capacity, eliminating allocation
per glass group. Whole-tile publication, edge clipping, eight-rectangle fallback
and exact layered refraction remain unchanged. Frames without glass skip bounds
projection and write tracking.

An isolated 1740x976 benchmark runs 240 frames with 220 full-screen reads/frame.
The paired old/new planner measurements on this machine were:

| Build / write workload | Old ms/frame | New ms/frame |
| --- | ---: | ---: |
| Release / small writes | 0.228 | 0.020 |
| Release / full writes | 1.068 | 0.063 |
| Debug / small writes | 43.635 | 0.161 |
| Debug / full writes | 106.209 | 0.960 |

Copy-volume checksums match. These synthetic timings establish the cost and
improvement of the planner, not the live scene's resulting frame time. The new
PlutoGESnapshotDamageTests target reports timing without machine-dependent pass
thresholds and compares randomized reads/writes with an independent tile oracle.
It covers multiword rows, clipped edge tiles, empty viewports and invalid partial
publication. Existing GPU tests compare exact rendered images.

Follow-up validation passed: the CPU target in Debug and Release, full Vulkan
and OpenGL RHI suites, and editor/runtime Release builds. The layered-glass image
check remains exact on both backends at 110,848 versus 691,200 copied pixels.

Profiler export and UI now split transparency CPU into bounds, grouping, damage
planning/publication, color-copy recording and surface recording. These are
non-overlapping accumulated CPU measurements, not GPU timings; opaque snapshot
setup and final pass teardown remain in the parent transparency scope. Compare
these fields in the next fixed-camera capture before choosing another structural
change. Timing instrumentation uses clock reads rather than a trace node per pane
to avoid expanding retained trace memory.
