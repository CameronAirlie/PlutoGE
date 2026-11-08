# Shadow throughput phase and renderer structural review

## Scope and evidence

The next implementation phase is documented in
[Retained packets and incremental glass snapshots](retained-packets-and-glass-snapshots.md).

This review follows the 240-frame capture covering sequences 4339–4578. Its mean
CPU frame was 21.516 ms, scene GPU 11.659 ms, transparency GPU 5.580 ms, and shadow
CPU 5.556 ms. These are observations from that workload, not a controlled before/
after benchmark of the changes below. GPU observations can repeat and CPU/GPU
rows are not frame-aligned. Inclusive scopes must not be added to their children.

## Implemented phase

1. **Incremental VSM preparation and batched submission.** Immutable packets keep
   their prepared chunk spans when neighboring packets change. Cluster fit is
   reevaluated when projection or bounds change. Caster and rigid draw tables
   upload changed record ranges, with a bulk fallback for scattered changes.
   Compatible contiguous rigid chunks share a storage draw table and one Vulkan
   multi-draw indirect call. Mesh and masked-texture boundaries end a batch.
   Indirect first-instance addressing selects each chunk without per-draw uniform
   changes. Instanced chunks and backends without the required capability retain
   individual submissions. OpenGL uses that fallback.
2. **More precise page membership and optional adaptive work.** Cluster culling
   uses conservative world AABBs against homogeneous page planes. Unknown bounds
   retain the sphere/uncullable fallback. Membership generations include extents.
   The light's new `VSM Adaptive Triangle Budget` property defaults off. When
   enabled, distinct completed scene GPU observations can grow the authored
   budget up to 4x, capped at 16 million triangles. Eight low-cost observations
   permit 25% growth; pressure reduces it by 25%, never below the authored budget.
   Current policy targets are 0.5 ms for page rasterization and 16.6 ms scene GPU.
   Missing/unidentified observations cannot cause growth. This is a throughput
   heuristic, not a GPU deadline guarantee, and must be calibrated on real scenes.
3. **Clipped glass footprints.** Known boxes crossing the eye plane are clipped
   along their edges instead of immediately becoming full-screen copies. Bounds
   include both the shader's clamped-W sample projection and the raster projection
   used by scene-UV fallback. Clipping samples to the physical near plane would be
   incorrect: refracted positions may lie before it. Unknown/deformed/nonfinite
   input and entirely behind-eye boxes retain conservative fallbacks. Valid bounds
   can still cover the entire screen; this optimization cannot remove true overlap.
4. **Order-independent point-shadow keys.** Global and per-face caster hashes use
   commutative, duplicate-preserving accumulation. Packet permutation no longer
   changes shadow content identity. Actual geometry, transforms, alpha/deformation,
   light projection and caster membership changes still invalidate the cache.

The profiler now reports CPU page batches, reused/rebuilt packet counts and the
effective triangle budget separately from GPU work and indirect command count.
These distinguish reduced CPU calls from reduced triangles.

## Larger structural findings, in priority order

### 1. Reconstructing render state still costs work proportional to the scene

**Evidence:** `Scene::SubmitRenderCommands` visits mesh, terrain and foliage
components each update. `Renderer::EnsureRenderCommandsSorted` allocates and moves
command/flag arrays even when reusing the previous permutation.
`Renderer::UpdateRenderCommandLods` traverses the submitted commands each viewport.
`RhiSceneRenderer::appendDraws` validates and reconciles visible, shadow and GI
packet lists separately. `RhiDrawPreparationCache::Entry::Matches` compares full
transform/history/bounds values; instance and pose arrays cannot use its immutable
fast path because their contents have no authoritative revisions.

The capture averages 1.360 ms in submission, 2.620 ms in viewport visibility,
0.806/0.664/0.801 ms in visible/shadow/GI packet preparation, and 0.625 ms in opaque
batching. Together these separate CPU scopes represent about 6.9 ms/frame.

**Consequence:** caching avoids some expensive reconstruction, but a mostly static
scene still pays traversal, comparison, copies and hashing across several layers.
Changing one actor can cause broad list work. Index-based downstream caches also
lose reuse when a preceding packet's chunk count changes.

**Recommended architecture:** a retained render-scene database with stable object,
material and geometry IDs; independent transform, topology, deformation and material
revisions; explicit dirty queues; and per-view lists of IDs. Share immutable object
data across visible/shadow/GI passes. Start with rigid meshes and preserve a
value-validated fallback for procedural callers. Add lifecycle/generation tests
before migrating animation or instancing.

### 2. Glass quality creates a serial copy dependency chain

**Evidence:** `BasicRenderer::renderTransparency` ends the current rendering pass,
copies color **and opaque depth**, and resumes rendering for each snapshot group.
The capture averages 187 copies and 199 million copied pixels/frame, approximately
117 viewport equivalents. Around 110 group boundaries/frame come from expanded
sample footprints and 76 from raster overlap. Transparency costs 2.882 ms CPU and
5.580 ms GPU on average.

**Consequence:** bounded copies help bandwidth but do not eliminate pass transitions
or the exact back-to-front dependencies. The opaque depth snapshot is repeatedly
copied even though the source is unchanged during this transparent pass.

**Recommended architecture:** split immutable opaque depth from mutable color
snapshots and copy depth once; introduce explicit refraction quality modes with
an opaque-background-only path for suitable materials and the exact layered path
for dependent panes. Preserve exact semantics by default. A pass-level dependency
planner could then batch independent regions and schedule reduced-resolution
refraction as an explicit quality choice. Weighted blended transparency alone
does not preserve refractive color-read dependencies.

### 3. Shadow representation remains tied to frontend geometry decisions

**Evidence:** RHI packet preparation selects `GetSubmeshLodRange` using
`command.lodIndex` for shadow packets as well as visible packets (GI emission has
its own override). The legacy `ShadowPass` has shadow-specific LOD selection,
including `minShadowLodIndex`; that policy is not applied in this RHI translation.
All 207 refreshed point faces in the capture reported both range/membership and
content changes. This is consistent with index-range churn, although the capture
alone cannot prove which actors or LOD transitions caused it.

**Consequence:** camera-driven geometry changes can invalidate otherwise stationary
shadow content. Sorting-only churn is fixed in this phase; real LOD range changes
remain valid invalidations and must not be hidden by a hash change.

**Recommended architecture:** independent shadow LOD selection based on projected
shadow texel error, with hysteresis and persistent per-light decisions. Share the
selection policy between legacy and RHI renderers. Add per-object invalidation
sampling so future captures can distinguish LOD, transform and material changes.

### 4. VSM membership is dense and GPU work still includes empty commands

**Evidence:** VSM allocates membership and page-list storage proportional to chunk
capacity times physical page capacity. The signature shader visits every chunk
for selected pages; the binning shader visits the update list for every chunk.
Multi-draw batching reduces CPU calls, but still processes the generated indirect
commands, including commands with zero instances. Geometry clusters are contiguous
1024-triangle index ranges, not guaranteed spatially compact clusters.

**Consequence:** adding pool pages or finer clusters can increase memory and
planning work even when few pages change. AABB culling improves this phase's
membership precision without changing that asymptotic cost.

**Recommended architecture:** spatially coherent imported clusters, then a coarse
caster hierarchy or spatial index to build sparse candidate pairs. Compact active
draws into material/geometry buckets and expose an indirect-count capability in
the RHI. Preserve conservative bounds and an uncullable fallback throughout.

### 5. Pass dependencies and resource ownership are still largely manual

**Evidence:** BasicRenderer coordinates shadows, geometry, transparency and many
post effects in one large recording flow. VSM uses broad `ShaderMemoryBarrier`
calls between compute passes; Vulkan implements these as all-commands memory
barriers. Storage-buffer uploads also add broad before/after barriers. A post-
process graph exists, but these shadow/transparency transitions are outside it.

**Consequence:** broad dependencies limit scheduling freedom and make lifetime,
barrier and cache correctness harder to maintain. This is a structural risk and
scaling concern; this capture does not establish barrier stalls as the dominant
GPU bottleneck. Fence waiting was not the explanation for its CPU frame time.

**Recommended architecture:** extend pass resource declarations to shadows,
transparency and uploads; derive resource-scoped barriers and transient lifetimes.
Introduce a bounded upload queue before changing synchronization. Extract pass
ownership and policy from BasicRenderer incrementally, keeping image-equivalence
tests at each step. Do not begin with a wholesale renderer rewrite.

## Next measurement

Capture the same camera path and stationary/animated intervals with identical
resolution, scene settings, debugger and profiling state. First compare fixed
budgets, then enable adaptive budgeting separately. Track CPU batches, packet
rebuilds, continuously requested page age, triangles, glass pixels and face
invalidations. Inspect actual shadow and refraction images as well as timings.
Synthetic tests validate correctness and mechanisms; they do not establish a
Bistro frame-time improvement.

## Validation

Release editor/runtime and both RHI test targets build successfully. Full Vulkan
and OpenGL rendering suites, VSM-only checks on both backends, Vulkan VSM
performance checks, and the CPU clipmap/cache tests pass. Membership caching
matches its uncached reference in all 20 scenarios on each backend (14 scenarios
change the resulting image).

The phase includes cross-backend glass image comparisons (including eye-plane
crossing geometry), point-cache packet permutation checks, incremental packet
invalidation checks, and batched/unbatched VSM image comparisons for opaque,
masked and mixed rigid/instanced casters. CPU policy tests cover repeated GPU
observations, headroom hysteresis, pressure backoff and hard budget limits.

The paired synthetic submission checks recorded 123 page API calls individually
versus 1 batched call for opaque and masked variants; the mixed instanced variant
used 5 batched/fallback calls for the same 123 indirect commands. Images matched
byte-for-byte. The animated-caster workload recorded 3 total indexed API calls
with batching, versus 125 in the preceding phase's log, at 105,600 submitted
triangles in both observations. These demonstrate submission reduction in those
workloads, not a prediction of full-scene speedup.
