# Renderer structural performance investigation

## Revised scope: core renderer with only tone mapping

The user has removed the other effects. The second attachment repeats frames 6385–6624 and the original timing summary, so it is not evidence of performance in that new configuration. The original analysis below is retained as historical context; GI and SSR recommendations are outside the revised scope.

The core-renderer priority is a visibility-first, feature-dependent rendering path:

1. **Compile per-view output requirements.** `BasicRenderer.cpp:1451` allocates normal/material/motion/albedo textures unconditionally, and `2239` binds them alongside HDR color; normal rendering removes only the debug output (`2279` vicinity). Five color outputs represent 28 nominal bytes per pixel before depth, compression, caches and overdraw; HDR alone is 8. Removing effects does not remove this geometry bandwidth or output calculations. Derive requirements from actual consumers (including debug views, temporal rendering and transparency), select compatible pipeline/shader variants, and omit unused allocation, attachment writes, clears, vertex history and fragment motion work. Avoid merely unbinding attachments while retaining incompatible pipelines. This reduces nominal color-output bytes by about 71% in a genuine color-only view, not total GPU time by 71%.
2. **Split material coverage and sidedness into pipeline classes.** Opaque materials currently inherit `CullMode::None` despite `draw.twoSided` already being supplied by material translation. Use existing sidedness to select correct opaque rasterization, handling mirrored winding. Separate opaque/no-discard, masked coverage, graph-deformed and transparent paths. For simple masked materials evaluate coverage before normal/metallic/roughness/emission inputs; graph-dependent alpha requires a dependency-aware coverage program. Do not force early depth writes on discard shaders and change coverage semantics.
3. **Build a shared current-frame visibility/depth stage.** Feed compatible depth to VSM receiver requests, occlusion and main opaque shading. Existing receiver reuse is restricted to unjittered all-safe-occluder views (`2259`), while the geometry pass starts its own rendering stage. Match deformation, alpha coverage, projection and jitter, and use depth read-only/equality-compatible shading where valid. Retain coarse front-to-back ordering within the state-change budget. Compare full prepass and selective occluder approaches because additional vertex/raster work can outweigh saved shading.
4. **Choose the lighting architecture after visibility.** For the stripped one/few-light workload, a lean forward path with depth rejection and only required outputs is a strong first implementation. For many lights, cluster light lists rather than looping all lights per fragment (`BasicLit.slang:818`). If lighting still dominates, use hybrid deferred standard opaque lighting and forward custom-graph/transparency exceptions. The current design pays forward lighting plus several G-buffer writes; eliminate that duplication intentionally. A full deferred conversion is not automatically faster for this small-light configuration.
5. **Parallelize VSM allocation if it remains material.** `VirtualShadowCompute.slang:allocateMain` performs initial work in parallel, then returns every lane except lane zero and serially assigns quotas, scans residents/free slots and assigns requests. Replace bulk serial scans/allocation with classification, prefix sums/compaction and disjoint parallel assignment, retaining the small global quota decision where useful. Preserve coarse-page reservations, residency identity, fairness and dirty age. The old capture attributes 0.586 ms to allocation per reported observation; that is an upper bound on savings from eliminating that measured work, not a promised saving or every-frame cost.

GPU-driven meshlet culling and indirect material submission become worthwhile when geometry/raster or scene-size measurements justify them. The old capture's roughly 90 geometry draws and 0.401 ms command translation do not justify making submission replacement the first major project. Meshlets can still reduce hidden geometry and tiny-triangle work, but need actual invocation/visibility evidence.

Implementation order: output requirements and material pipeline classes; shared depth/visibility and occlusion; geometry diagnostic A/B measurements; choose lean forward/clustered or hybrid deferred based on remaining cost; then VSM planning and finer geometry culling. No renderer implementation or new runtime benchmark was performed in this investigation.

Evidence: supplied capture, frames 6385–6624, and current source. This is a source review, not a measured implementation benchmark. No renderer behavior was changed.

## Capture interpretation

All 240 frames use normal rendering, 490 × 231 internal resolution, 980 × 461 output, and disabled occlusion culling. The capture contains 240 distinct scene GPU observation IDs. Individual pass observations remain asynchronous; intermittent pass results must not be treated as aligned per-frame costs or event frequencies. Parent scopes include children.

| Measurement | Mean | Interpretation |
|---|---:|---|
| Scene GPU | 14.463 ms | Main throughput concern |
| Opaque and alpha-tested GPU | 7.720 ms | About 53% of mean scene GPU time |
| VCT GI GPU | 1.681 ms | Median 0.637; p99 21.275 ms |
| VCT volume publication | 10.435 ms | Seven reported observations; includes nested work |
| SSR GPU | 1.071 ms | Trace alone 0.981 ms |
| Temporal upscaler GPU | 1.094 ms | Significant relative to this small output |
| Transparency GPU | 0.221 ms | Low priority in this workload |
| Scene active CPU | 4.173 ms | Parsed mean across all frames |
| Command translation CPU | 0.401 ms | Existing retention is already helping |
| Swapchain acquisition | 8.796 ms | Waiting/pacing with VSync enabled |
| Scene frame fence wait | 0.010 ms | No large wait at this particular boundary |

CPU frame mean is 17.198 ms, but acquisition waiting is not equivalent to renderer CPU computation. The debugger is attached. Keep executable/configuration fixed during comparisons; add an uncapped capture to measure throughput separately from presentation pacing.

## 1. Make GI publication incremental in time, not only in space

**Strongest concrete structural finding.** `BasicRenderer::PublishVctCascade` (BasicRenderer.cpp:3535) resolves the same base data separately into six directional atlases, then dispatches every direction at every mip, with inter-level barriers. Dirty regions already exist: adding dirty-region support is not a new solution. Staging, invalid volumes, secondary changes, and gain changes can require full-volume publication.

Voxel draw/index budgets and secondary slice budgets do not bound this publication work. `AdvanceVctSecondary` calls publication both to construct a direct-only staging source (3391) and after finishing secondary gathering (3421). The named `RHI VCT Volume Publish` scope is instead around the rebuild completion block (4016); it includes an optional relight and does not cover every invocation of the publication function. Consequently the existing publication and gather timers cannot fully explain the GI tail.

Recommended changes:

1. Instrument inside publication, tagging direct staging, direct publication, and secondary publication; split base resolve, mip generation, and relighting timings. Report voxels, mip regions and publication count alongside observation IDs.
2. Turn publication into a resumable work queue: base tiles, directional mip tiles, then commit. Bound work per frame using measured cost estimates as well as dispatch/voxel limits. A command count alone is not a GPU time guarantee.
3. Keep the old complete version readable while building the new one. Commit texture/version and origin/size metadata together only after all dependent levels are ready. Use ping-pong storage or versioned bricks; avoid exposing partially updated mip chains. Account for the additional memory and update latency.
4. Share the isotropic base level between directions, or resolve it once and feed the six first directional mips from it. The six base dispatches currently bind the same inputs with no directional parameter. Sampling/layout changes are needed to remove redundant base storage safely.
5. Longer term, use independently versioned bricks with dirty ancestor propagation. Preserve conservative invalidation for secondary-light propagation, which can extend beyond the direct-light dirty region.

Goal: substantially lower GI p95/p99 spikes. Spreading work reduces peak cost; sharing base work can also reduce total cost. Neither guarantees a specific FPS increase without measurement.

## 2. Separate opaque visibility from lighting

**Largest steady-state opportunity, magnitude unconfirmed.** The opaque pass costs 7.72 ms for only 113,190 internal pixels. This is evidence to investigate shader cost, overdraw and geometry efficiency, not proof of which dominates.

`Renderer.cpp:299` sorts by shader/material/mesh/range rather than depth. `BasicRenderer.cpp:597` uses no face culling for the base geometry pipeline. The opaque loop at 2533 shades draws directly. `BasicLit.slang:702` performs material evaluation and then directional/local/environment lighting while producing material, normal, motion and albedo outputs. Alpha discard follows multiple material texture reads and optional graph evaluation. Standard materials already have a graph-free variant (`BasicLitStandard.slang`); recommending that optimization again would miss the current implementation.

Recommended staged architecture:

* First compare coarse front-to-back depth bins, retaining material grouping within bins. Measure the state-change tradeoff.
* Add explicit material sidedness and use backface culling for verified single-sided assets. Do not globally cull imported interiors, foliage or intentionally two-sided surfaces; handle mirrored transforms.
* Introduce a shared depth/visibility pass, then shade opaque survivors against that depth. Reuse its depth for VSM receiver requests and the occlusion hierarchy where projections, jitter and coverage agree. Avoid introducing three redundant depth passes. Alpha-tested and graph-deformed geometry need matching coverage/deformation rules; retain a fallback where necessary.
* If diagnostics show lighting dominates, move standard opaque lighting to a deferred screen-space pass using the already-produced surface data. Keep custom per-light graph materials and transparency forward. This bounds ordinary lighting to visible pixels and permits coherent shadow/light processing, at the expense of extra bandwidth and a more complex material split.

Run existing MinimalShading, MaterialOnly, BypassDirectionalShadows, BypassDetailTextures, BypassPointLights, BypassSkyLighting and DrawRanges diagnostics before choosing the larger rewrite. A depth prepass doubles some geometry work and can lose on vertex-bound or low-overdraw scenes. Small triangles at this internal resolution are another plausible cause; check GPU invocation counters and LOD sensitivity.

The existing GPU occlusion path is disabled throughout the capture. Evaluate its measure/cull modes before building a replacement. Its safe depth occluders currently exclude graph materials, non-opaque surfaces and multi-instance draws (`OcclusionCulling.cpp:39`). Its benefit therefore depends on actual scene occlusion and coverage.

## 3. Decouple directional shadow visibility from material shading if its diagnostic wins

`BasicLit.slang:351` performs VSM level selection, page-footprint residency checks, and filtered atlas sampling within surface shading. It already has a four-gather fast path for the common filter footprint; this is not uniformly a naive large scalar tap loop. Cached shadow pages save production work but do not eliminate this receiver cost.

If bypassing directional shadows materially reduces geometry time, resolve directional visibility once for visible opaque receivers from depth/geometric information, then consume it during lighting. A deferred opaque path is the natural home. Keep transparent receivers separate and preserve receiver-plane bias, page fallback and edge correctness. Temporal or reduced-resolution shadow filtering is an optional quality tradeoff, not an appearance-preserving free optimization.

Do not infer receiver cost from the 0.835 ms shadow-page timer: that measures different work.

## 4. Replace fixed 16-ray SSR integration with temporal sampling

`SSR.slang:135` fixes the integration at 16 GGX rays per trace pixel; each ray can march up to the configured 48 steps, plus refinements. The capture uses 245 × 116 trace pixels. Early exits and rejected rays mean this is a configured upper workload, not an observed sample count.

A structural alternative is one or a few stochastic rays per frame with motion reprojection, depth/normal/material rejection, neighborhood clamping and spatial denoising. Add a depth hierarchy and roughness/reflectivity tile classification to skip empty work. Share hierarchy infrastructure where depth convention and reduction requirements match; occlusion and SSR may require different reductions.

This is a quality/temporal stability tradeoff requiring tests for disocclusion, thin objects, moving reflectors and rough surfaces. Its entire current budget is about 1.07 ms, so prioritize it below geometry and GI spikes.

## Lower priorities and validation

GPU-driven scene submission, broad multithreaded recording, and a new glass architecture are not the first investments for this capture: translation averages 0.401 ms and transparency 0.221 ms. Retained scene/packet reuse and incremental glass snapshots already exist. Large-scene workloads may rank these differently.

A render graph with explicit resource accesses could improve scheduling and replace broad barriers, but is an enabling architecture rather than a demonstrated multi-millisecond saving here. Do not assume asynchronous compute overlaps effectively when these workloads compete for bandwidth or have dependencies.

Validate each stage with fixed-camera and moving-camera captures, identical resolution/quality/build, settled temporal history, and distinct GPU observations. Record both uncapped throughput and VSync frame pacing. Compare median and p95/p99, publication volume/count, geometry invocation counters if available, and image correctness. Include masked geometry, material graphs, mirrored meshes, moving lights, GI invalidation, disocclusion and transparent surfaces. Use render comparison tests for implementation changes; no build or runtime tests were run for this read-only investigation.

Suggested order: complete GI timing coverage and budget publication; run geometry diagnostic captures; prototype shared visibility/depth; pursue hybrid deferred lighting only if the measured shader breakdown supports it; then temporal SSR.
