# DungeonCrawler rendering architecture investigation

Investigation: 23 September 2026. This follows the CPU structural pass. No production
rendering settings or renderer implementation were changed during this investigation.
The benchmark runner now supports explicit diagnostic variants in staged assets.

## Conclusion

The largest demonstrated opportunities are incremental GI publication and shadow
planning that scales with changed casters/pages. Another general gameplay/ECS rewrite
is not justified by this workload. Rendering is expensive even with fairly few visible
draws because the engine performs voxel lighting, volume mip construction, virtual
shadow planning, and several screen-space effects around those draws.

Feature-removal measurements below are diagnostic ceilings, not proposed quality
reductions or promised savings. Optimised replacements retain some of the removed
work, and CPU throughput limits the eventual FPS improvement.

## Measurement protocol

Release Vulkan runtime, 1280x720, VSync off, 240 warmup and 1,800 measured frames per
run. All runs use the same native executable and captured managed assembly, the same
player route, and 16 living enemies. No builds run during measurement. Each run stages
fresh assets and an isolated save. Streamline is disabled as in the existing fixture.
The scenario excludes editor overhead and player attacks.

Run order: Default, NoSecondaryBounce, NoLocalLightInjection, NoGI, LegacyShadows,
NoShadows, NoGIOrShadows, Default, then the six variants in reverse order, then Default.
There are three default controls and two runs per variant. Raw paths are saved in
`out/build/architecture-investigation.json`; each path contains the staged project,
workload validation, frame CSV, GPU scope CSV and summary.

Background applications remain active; an initial GPU snapshot showed 23% utilisation
before these runs and a post-run snapshot showed 35%. These are repeated comparisons on this machine, not uncontended
hardware specifications. GPU observations are asynchronous and may repeat; named
parent/child scopes overlap and must not be added indiscriminately.

All 15 runs passed, covering 27,000 measured frames. Runtime, managed assembly and
source scene hashes match across every run. The aggregate is saved in
`out/build/architecture-investigation-aggregate.json`. Frame mean/p95 pool all frames
within each variant; FPS is 1,000 divided by pooled mean, not averaged run FPS.

| Diagnostic variant | Mean frame | Frame p95 | FPS | GPU scene |
|---|---:|---:|---:|---:|
| Current settings | 3.349 ms | 4.04 ms | 298.6 | 3.25 ms |
| Secondary bounce disabled | 3.241 ms | 4.09 ms | 308.5 | 3.13 ms |
| Local-light GI injection disabled | 2.796 ms | 3.42 ms | 357.7 | 2.68 ms |
| GI disabled | 2.562 ms | 3.50 ms | 390.4 | 2.36 ms |
| Directional legacy cascaded shadows | 4.198 ms | 5.06 ms | 238.2 | 4.17 ms |
| Light shadows disabled | 2.315 ms | 3.01 ms | 431.9 | 2.13 ms |
| GI and light shadows disabled | 2.078 ms | 2.98 ms | 481.3 | 1.27 ms |

The three default runs span 288.5–309.6 FPS. The secondary-bounce result is small
relative to that variation and does not establish a useful FPS win. The larger GI and
shadow changes reproduce in both orders. The combined removal reaches 452.7–513.8 FPS;
its GPU time is only 1.27 ms on average while the frame takes 2.08 ms, exposing a CPU
and submission throughput limit. These results do not support promising a same-quality
doubling of FPS from the next single change.

Default GPU scope averages identify where to start:

- GI: 0.878 ms, including volume publication at 0.464 ms; surface relighting itself is
  only 0.026 ms. Disabling local-light injection reduces publication to 0.007 ms and GI
  to 0.325 ms while shadow planning remains essentially unchanged. This is strong
  evidence that publication downstream of moving-light updates is the GI target.
- VSM planning: 0.615 ms, including caster signatures at 0.224 ms, allocation at
  0.201 ms and budgeting at 0.103 ms. Optimising receiver requests again would miss
  the dominant planning work.
- Geometry: 0.573 ms. GI and planning together exceed the geometry pass in this fixture.

These are costs to optimise, not fully recoverable frame-time savings. For example,
half the measured publication cost would be about 0.23 ms of GPU work, but is not a
prediction of total frame improvement. Replacing all GI and shadows removed about
1.27 ms of wall-clock frame time; preserving those features necessarily retains work.

## Structural changes, in implementation order

### 1. Preserve changed-region information through GI publication

`VctChangedLightRegion` already bounds direct relighting using both the old and new
light influence. The optimisation is incomplete downstream: `PublishVctCascade`
resolves the entire volume separately into six directional atlases, then dispatches
every mip for every direction. At the configured 64 resolution, that is six base
dispatches plus 36 mip dispatches, with a broad memory barrier after each dispatch,
per publication. The base resolve shader performs identical radiance/opacity work for
each destination; directional differences begin in the mip shader. Secondary gathering
also publishes a direct-light-only staging field.

Implement a publication plan owned by each cascade: dirty region(s), reason, generation,
and full-refresh fallback. Carry direct-light dirtiness into base resolve and all
affected mip regions. Geometry changes, cascade relocation, and uncertain shader
dependencies must conservatively request a full update. Expand regions for the opacity
neighbour stencil; round and propagate mip boundaries correctly. Secondary propagation
has wider dependencies than the direct-light region and needs its own conservative
invalidation, not the same small box applied blindly.

First remove redundant base computation (a shared base volume, or supported multi-output
resolve) and schedule independent directional work together. Then implement partial
publication. Choose a shared-base representation or capability-aware multi-output path
without exceeding lower-end/OpenGL storage-image binding limits. Separate static
geometry/emission revisions from moving-light revisions so a moving torch cannot
invalidate voxel geometry accidentally. Existing code already separates some of these
lifetimes; extend it rather than introducing a second competing cache.

Validation: compare full versus incremental outputs for light movement/removal, moving
and removed geometry, alpha-tested/emissive material edits, volume relocation, secondary
bounce changes and scene reload. Check stale light trails, seams and bounds on both
Vulkan and OpenGL. Export publications, dirty voxels, full-refresh reasons and dispatch
counts alongside timings. Preserve current update cadence and visual settings for the
first comparison.

### 2. Replace exhaustive VSM planning with persistent spatial membership

`VirtualShadowCompute.slang` uses a single GPU invocation for allocation and another
for budgeting. Signature evaluation scans every caster for every resident page;
binning scans the page capacity again for every caster. Capacity is 256 pages. The
whole-frame reuse shortcut is invalidated by camera or caster changes, so animated
enemies and the moving camera prevent it from addressing this normal gameplay case.

Introduce stable caster handles with generations, spatial membership per clipmap/light,
and dirty-page queues. Moving/removing a caster invalidates the union of its old and new
coverage. New/evicted pages and light/clipmap changes rebuild membership conservatively.
Reuse unchanged page signatures and static membership. Compact requested/free/dirty
pages on the GPU; allocate and budget using parallel scans/partitioning while retaining
coarse coverage, per-level quotas, deterministic ownership and existing budget semantics.
Bin only selected update pages rather than scanning every page capacity for every caster.

A later static/dynamic depth split could avoid redrawing dungeon geometry when only an
enemy moves. It is a larger memory and sampling tradeoff: static and dynamic occlusion
must compose correctly and dynamic depths must clear without leaving ghost shadows.
Measure this separately after planning is improved.

Validation: existing shadow cache/clipmap/rendering tests plus moving/skinned casters,
deletion/address reuse, teleportation, spotlight motion, alpha material changes, page
pressure, coarse coverage, starvation and CPU-reference visibility. Track requested,
resident, dirty, updated, deferred and overflow counts so faster planning cannot conceal
missing shadows. The directional CSM diagnostic is not a recommendation to switch
algorithms: local lights still use VSM and the combined path costs more here.

### 3. Add explicit resource dependencies to the RHI

`VulkanCommandContext::ShaderMemoryBarrier` currently emits an ALL_COMMANDS-to-
ALL_COMMANDS memory barrier. GI repeatedly invokes it between independent destinations.
Introduce resource/subresource access declarations and batch independent writes, with
one correctly scoped transition at actual producer/consumer boundaries. Begin with GI
publication and shadow planning; extend toward a render graph once those uses validate
the contract. Keep backend-specific barrier translation behind the RHI.

This is supported by code inspection, not a measured standalone barrier saving. Do not
simply remove barriers or promise async-compute gains. Validate hazards across mips,
frames in flight, external upscalers and resource retirement. Async compute is a later
experiment: dependencies and competing memory bandwidth can erase its benefit.

### 4. Revisit CPU rendering throughput after the GPU reductions

The baseline already spends roughly half a millisecond deforming meshes plus upload
time. Once GI/shadow cost falls, compute skinning becomes a reasonable next architecture:
upload bone palettes, deform once into a GPU buffer shared by geometry/shadow passes,
retain previous positions for motion vectors, and provide conservative bounds without
synchronous GPU readback. Preserve the CPU path for unsupported devices and validate
attachments, history resets, topology changes and culling. GPU skinning consumes GPU
time, so it is not automatically beneficial while the existing GPU workload dominates.

Scene update and visible geometry submission are substantially smaller than the major
GPU passes here. A generic ECS rewrite, pervasive multithreading, or replacing all UI
layout is not the first investment. Nor is adding more frames in flight: the scene
command context already has three frame resources. Queue-idle calls found in the Vulkan
backend belong to immediate texture upload/readback paths; the current evidence does
not establish a queue-idle call every steady-state frame. Instrument fence, acquire,
present and immediate-upload counts before proposing a submission-system rewrite.

## Alternative product-level rendering architecture

For lower-end hardware, baked or cached static irradiance plus dynamic direct lights
could remove much of the continuous voxel GI workload. Dynamic shadowed characters can
remain. This is a valid optional rendering tier, but changes moving-light bounce,
destruction response and authoring workflow. It is not equivalent to preserving the
current GI. Existing world-cache support is not the same as a baked static-only mode;
it adds a stationary source cascade and should not be assumed cheaper without testing.

## Acceptance gates

Land each subsystem change separately, compare against the same executable/settings
baseline with interleaved repeated runs, and require a gain beyond baseline variation
without worse p95 or visual regressions. Add a combat/streaming route and test the actual
lower-end target device before selecting defaults. Establish the target resolution,
hardware and frame-time budget for any subsequent quality-tier decision. Do not add
individual ablation savings to predict combined FPS.
