# VSM quality and freshness

## Configuration

Directional light properties are serialized through the existing component property system and translated by `ApplyDirectionalShadowSettings`. Direct RHI users set the corresponding `BasicLighting::virtualShadow*` fields. These settings govern the shared directional/spot VSM renderer.

| Property | Default | Behavior |
| --- | --- | --- |
| VSM Pool Pages | 256 | Rounds up to 256, 576, or 1024 physical pages; atlas attachments use 32, 72, or 128 MiB. Metadata, membership, and receiver targets are additional. |
| VSM Page Updates per Frame | 64 | 1–1024, clamped to the active physical capacity. |
| VSM Triangle Budget per Frame | 1,000,000 | Normal aggregate page-rendering allowance. |
| VSM Maximum Page Age | 8 | Age at which a dirty page receives overdue priority; not a hard latency guarantee. |
| VSM Allow Oversized Pages | true | One aged page costing more than the entire triangle allowance may render first in a frame. No additional positive-cost pages render that frame. Disable for a strict triangle cap, accepting possible indefinite deferral. |
| VSM Spotlight Resolution | 2048 | Rounds up to 512, 1024, or 2048. Only fine pages requested by visible receivers are allocated. |
| VSM Cluster Culling | true | Use mesh index-range clusters and tighter per-instance-group world bounds. Disable for comparison. |
| VSM Coarse Minimum Caster Texels | 0.5 | Reject directional coarse casters with smaller projected bounding-sphere radii; zero disables this approximation. Fine pages retain all intersecting casters. |

Pool resizing replaces atlas and membership resources, invalidates residency, and abandons old asynchronous statistics through a new callback owner. GPU resources follow the backend's normal retirement mechanism. A quality change therefore has a warm-up period. No synchronous diagnostic readback is added.

## Policy and scheduling

`VirtualShadowResolutionPolicy` owns only directional resolution and hysteresis. GPU counters distinguish directional fine demand, local fine demand, coarse reservations, and the capacity left for directional refinement. Spotlight requests never masquerade as directional requests. Overflow doubles directional texel width, up to 16 times the base value. Recovery requires 32 fresh low-pressure samples after a 32-frame settling interval; projected fourfold demand must retain 20% capacity headroom, and deferred updates must clear before refinement resumes. Duplicate or pre-change observations cannot advance recovery.

GPU scheduling sorts compact candidates in shared memory. Overdue pages come first, followed by new coarse coverage, dirty resident shadows, and new fine coverage. Older pages precede younger ones in each class; a rotating order breaks ties. Dirty age begins at the first unsatisfied content change and survives subsequent caster motion. Publishing a completed update clears it. This prevents new requests from permanently outranking moving shadows, but throughput and continuous eviction still limit freshness.

The oversized-page exception is explicit rather than hidden in budget accounting. The profiler reports actual triangles, oldest pending age, and the number of oversized updates. A single very expensive page can cause a frame-time spike. Clustering and lower-detail source meshes are preferable to depending on this escape routinely.

The allocator uses bounded shared lists and persistent page metadata; the scheduler uses a compact sorting array. No shared-memory allocation grows beyond the compile-time 1024-page ceiling. Runtime atlas dimensions and list strides are shared in the C++/Slang parameter layout.

## Geometry

`ShadowGeometry.h` builds conservative bounds for contiguous groups of at most 1024 triangles without changing GPU indices or material ranges. VSM intersects those ranges with the submitted submesh and divides instance arrays into groups of at most 16. Rigid meshes whose conservative projected diameter fits within one fine page are coalesced back into a single range to avoid unnecessary CPU submissions. Each group receives a conservative world sphere computed from transformed local AABBs, including nonuniform scale and shear. Mesh vertex updates rebuild cluster bounds when geometry changes.

Index locality determines how tight these clusters are. This is bounded cluster culling, not geometry simplification or Nanite. Existing selected mesh LODs are honored because clustering operates on the submitted mesh. No automatic shadow-only LOD generator is introduced. If clustering would exceed the existing 4096-command limit, preparation falls back to whole-draw chunks; invalid bounds remain conservative. CPU index copies and cluster bounds add mesh memory, and cluster submission can increase CPU draw work. The comparison toggle and GPU counters make that tradeoff measurable.

## Spotlight sampling

Each of the first four eligible spots owns a fine projection and a matching 256-square coarse projection. Coarse coverage requires four pages per light instead of reserving the entire fine map. Receiver-depth requests include the filter guard. Fine sampling uses a continuous tent with receiver-plane depth correction; if any footprint page is missing, the entire sample uses coarse coverage instead of treating missing taps as lit. Filter width is scaled across fallback levels to avoid multiplying world-space softness.

This filter removes the old integer-centered nine-tap stepping. It does not implement SMRT/contact-hardening shadows. Local fine demand can still exceed the pool; larger pools and lower spotlight resolution remain explicit quality/performance choices.

## Validation entry points

- `PlutoGEVirtualShadowClipmapTests`: resolution hysteresis, pool tiers, conservative cluster bounds, deformation, and projection stability.
- `PlutoGERhiPostProcessAdapterTests`: serialized controls and editor/runtime translation.
- `PlutoGEVulkanRhiTests --shadows-only` and the OpenGL equivalent: shadow filtering, spotlight resolution tiers, and self-shadow checks.
- `--vsm-only` on either RHI executable: live pool resizing, strict-budget deferral, oversized-page forward progress, continuity, and root fallback.
- `PlutoGEVulkanRhiTests --vsm-performance`: cache convergence, actual triangle/update limits, and cluster-bounded submission under camera/caster motion.

Target-scene quality and timing should be assessed with the same camera, lights, resolution, and hardware. Synthetic results do not establish a universal performance gain.

## Verified results (28 September 2026)

Release builds passed for the editor, runtime, both RHI test executables, clipmap/policy tests, and the post-process adapter tests. Slang compiled the affected shaders for Vulkan and OpenGL. The full Vulkan and OpenGL rendering suites passed, as did `--vsm-only` on both backends. Vulkan's `--vsm-membership` matched cached and recomputed intersections across 20 scenarios (14 changed images). The three focused CPU CTest cases passed.

The GPU checks exercised all three physical pool tiers, strict one-triangle deferral, oversized-page convergence, all three spotlight resolution tiers, alpha masks, instancing, moving casters, oblique receivers, grazing lights, and fog coverage. Pool resizing preserved the settled shadow image. Spotlight self-shadow checks reported zero acne pixels in the tested receiver angles on both APIs.

The updated 582×507 synthetic Vulkan workload on an NVIDIA GeForce RTX 4070 SUPER compares whole-mesh submission and clustered/coalesced submission within the new renderer. It includes many small casters and a larger mesh that requires subdivision. Both paths produce byte-identical settled images. During caster motion, average submitted shadow triangles fell from 768,000 to 105,600 (86.25% fewer). The final capture measured 1.38 versus 1.22 ms GPU frame time and 0.087 versus 0.083 ms shadow CPU time. GPU timings varied substantially between captures; these figures are observations, not a promised speedup. The subsequent animated-caster scenario had zero deferred pages and 126 cache hits; stationary rendering performed no shadow updates or shadow draws.

Small projected meshes are coalesced because blindly submitting every cluster increased overhead in an earlier test. Keep the cluster-culling toggle available when evaluating other content. Deforming mesh updates rebuild bounds on the CPU, and very fragmented scenes can fall back to whole-draw submission at the command limit. Larger pools increase memory and can expose additional rendering demand; oversized updates trade occasional extra work for forward progress. The user's game scene has not been visually or performance revalidated by these synthetic checks.
