# SSS fidelity-preserving optimisation plan

Date: 2026-09-15. Status: first implementation increment built and tested.

## Implementation log

### First increment — skinning bounds and actionable profiling

- Folded the existing bounds reduction into CPU vertex deformation, removing the second full vertex scan. The bounds formula, deformation arithmetic, cache ownership and temporal-history rules are retained.
- Added separate deformation/bounds and skinned-upload CPU timings, plus update/vertex counts, to both the profiler panel and copied reports. Renamed the inclusive mesh metric to make its skinning work explicit.
- Added nested SSR trace/resolve GPU scopes using the existing asynchronous timing infrastructure. The existing SSR benchmark now reports and validates these scopes.
- Extended skinning checks for exact bounds against an independent scan, reflected transforms, negative coordinates, single vertices and empty input.

This increment does not change visual settings or move work to the GPU. Full baseline capture, geometry investigation and SSR traversal optimisation remain pending. No SSS FPS improvement is claimed from this change alone.

Validation: `msvc-nvidia` RelWithDebInfo editor, Vulkan/OpenGL RHI test executables and both profiler test executables built successfully. All seven focused tests passed: Vulkan skinning, Vulkan/OpenGL SSR, Vulkan/OpenGL temporal motion, editor profiler and profiler panel. `git diff --check` passed. These are synthetic rendering/correctness checks, not a full SSS visual comparison or before/after frame-time benchmark.

Rebuilt editor: `out/build/msvc-nvidia/editor/RelWithDebInfo/PlutoGEEditor.exe`. Build and test logs: `out/build/sss-optimisation-build.log`, `out/build/sss-optimisation-vulkan-tests.log`, `out/build/sss-optimisation-other-tests.log`. The pre-existing `CMakePresets.json` modification was left untouched.

## Findings and limits

Inspected the supplied capture (CPU frame 86817), SSS project and Main scene, its editor post-process preset, and the current RHI renderer/shaders. This is a source investigation, not a new live GPU capture. Hardware, running binary/build configuration and actual internal rendering dimensions remain unverified. A debugger was attached in the capture.

47 FPS corresponds to 21.28 ms/frame. The supplied CPU frame is 27.56 ms (36.3 FPS if sustained), and the asynchronous scene GPU observation is 23.305 ms. These describe different observations; they do not establish a sustained baseline or add together. The editor viewport is 1005 × 594, which does not establish internal resolution under temporal upscaling. The project selects Vulkan and runtime FSR2 Quality; the editor's effective upscaler must be recorded separately.

| Priority | Observed work | Cost | Interpretation |
| --- | --- | --- | --- |
| 1 | Geometry | 10.96 ms GPU | Largest individual GPU scope. Includes lit material shading and surface VSM filtering, not just triangle processing. |
| 2 | SSR | 2.78 ms GPU | Largest named post effect; already traces at half resolution. |
| 2 | Skeletal deformation | 6.897 ms CPU elapsed | Largest active CPU trace. Animation pose evaluation itself is only 0.64 ms. |
| 3 | VSM receiver/planning/pages | 0.50 / 0.92 / 1.03 ms GPU | Separate from receiver filtering inside geometry. |
| 3 | Temporal upscaler | 1.55 ms GPU | Preserve current reconstruction quality; verify input/output dimensions. |
| 3 | VCT GI / SSAO | 1.54 / 1.05 ms GPU | GI caching and half-resolution AO already exist. |
| 4 | Fog / clouds | 0.90 / 0.59 ms GPU | Existing reduced-resolution paths; lower priority. |
| 4 | Bloom / exposure / tone mapping | 0.43 / 0.18 / 0.14 ms GPU | Small costs; avoid spending the first optimisation pass here. |

`RHI Post Process` (7.06 ms) encloses child effects. `RHI Output Post Process` (0.85 ms) similarly groups output work. Do not sum parents and children. The upscaler closes the internal post scope and starts the output scope in BasicRenderer. Individual asynchronous observations also need matching GPU frame IDs before summation.

Presentation spends 8.20 ms waiting for a fence and 2.67 ms in queue present. This is consistent with GPU pressure, but one capture cannot prove the sole bottleneck. It is not evidence of 10.87 ms of independently removable CPU computation. Descriptor allocation and writes are zero; mesh-upload attempts are zero despite 7.33 ms in the broad mesh metric, because that metric also includes skinning and dynamic vertex updates. Legacy geometry counters reporting zero do not mean no geometry was drawn: the RHI records 82 draws.

## Phase 0 — establish a trustworthy baseline

1. Record GPU/driver, executable path, source revision, build type, backend, debugger state, effective upscaler, internal/output dimensions, preset and camera. Benchmark an optimised build without a debugger and retain a like-for-like capture of the current configuration.
2. Use the same SSS camera route and animation playback with static-camera, moving-camera, close character, reflective surfaces and shadow-boundary segments. Warm shaders and temporal/world caches; collect at least 300 measured frames per segment, repeat three times, and report median, p95 and p99 frame times. Report cold-cache traversal separately.
3. Attach frame IDs, parent IDs and resolution to GPU scopes; distinguish inclusive versus exclusive timings. Expose skinning vertex/update counts already collected by the translator, plus deformation, bounds reduction and buffer-upload time separately. Add SSR trace/resolve and VCT update/trace/resolve timings. Use asynchronous timestamp readback without introducing waits.
4. Capture GPU counters for geometry: vertex/fragment invocations, overdraw, bandwidth, shader occupancy and stalls. Run temporary diagnostic variants isolating shadow sampling and material shading. Such variants diagnose cost; they are not proposed quality settings.

Assumed first milestone: sustained 60 FPS at the current visual settings and a documented resolution. That needs 4.61 ms off a sustained 47-FPS frame budget. The sampled scene GPU time would need approximately 6.64 ms reduction to reach 16.67 ms, before allowing headroom for editor/presentation GPU work. Neither figure is a promised saving. Re-rank after the baseline.

## Phase 1 — reduce geometry shading cost

**Ownership:** BasicRenderer for pass scheduling; BasicLit and shared shadow helpers for shading; VirtualShadowMaps for page resources and residency.

`BasicLit.slang` performs VSM page lookup and a weighted tent of depth comparisons per receiver. Page mappings are already reused within a footprint. Therefore, simply proposing page-table reuse or changing the scene's screen-space filter scale would miss the current implementation.

First candidates, selected by measured counters:

- Reduce repeated address and weight calculations in the VSM tent, preserving all contributing samples, receiver-plane depth correction, normal bias, coarser-level fallback and page-boundary behaviour. Evaluate gather-based loads only where backend support and layout permit; verify each comparison keeps its own corrected depth. Hardware comparison filtering is not automatically equivalent to the existing per-texel correction.
- Measure opaque front-to-back ordering within compatible material groups. Keep batching and stable temporal object identity; preserve transparent ordering. Adopt only if reduced overdraw exceeds CPU/state-change costs.
- Inspect the existing VSM receiver-depth pass for potential main-pass depth reuse. Prototype only when camera/jitter, coverage, alpha discard, deformation and depth conventions match. Account for extra vertex work and barriers; do not assume a prepass wins.
- Audit the six geometry colour attachments, particularly the diagnostic shadow/LOD output. Allocate/write diagnostics only when consumed; retain motion, normals, material and albedo required by temporal/post effects. Confirm all consumers before changing formats or layouts.

Gate: lower total GPU frame time at unchanged output, with no shadow leaks, edge instability, missing alpha-tested coverage or motion-vector changes. The 10.96 ms scope is an opportunity ceiling, not a forecast of savings. Do not begin with a renderer-wide deferred rewrite.

## Phase 2 — SSR traversal

**Ownership:** SSR shader and a renderer-owned shared screen-depth hierarchy if justified; resource lifetime and barriers through the RHI.

Current preset: 48 steps, five binary refinements, 30-unit distance, intensity 0.8. The shader integrates 16 fixed GGX samples per trace pixel: up to 768 main march iterations before refinement, with early exits reducing actual work. The existing half-resolution trace and full-resolution depth/normal/material-aware reconstruction must be retained as the reference.

1. Measure trace versus reconstruction. Optimise invariant arithmetic and resource access while retaining the same rays, weights and hit acceptance.
2. Prototype a conservative reverse-Z depth hierarchy to skip empty intervals, shared with future screen-space consumers instead of embedding duplicate pyramids in each effect. Include hierarchy construction and barriers in the benchmark.
3. Preserve the current first-crossing and thickness semantics. For exact-reference mode, skip only intervals proven not to contain a hit and retain the original candidate sample positions/refinement. Test thin geometry and screen exits explicitly; a different traversal is not inherently image-equivalent.
4. Consider fewer rays with temporal reuse only as a later, separately reviewed visual tradeoff if exact optimisations are insufficient. It introduces reflection lag, ghosting and disocclusion risks and is not the initial recommendation.

Gate: same reflection coverage, roughness response and thin-object detail in stills and motion; net frame-time improvement including new supporting passes.

## Phase 2 — CPU skinning, independently benchmarked

**Ownership:** a renderer-owned deformation service using the existing RhiSkinning boundary. Animation remains responsible for poses, not backend buffers.

Current code already deforms once per mesh/pose per rendering frame, reuses the result across passes, retains previous positions and skips unchanged poses. Do not reimplement those caches. The measured deformation scope also encloses a subsequent full vertex bounds scan.

1. Profile the optimised build first. Fuse bounds accumulation into deformation, preserving the same conservative bounds. Evaluate compiler vectorisation/SIMD while retaining weight validation, inverse-transpose normals, reflected tangent handedness and degenerate-matrix fallbacks.
2. If warranted, use the engine's job infrastructure for independent deformation work, with explicit pose snapshots and bounded tasks. Keep device uploads on their permitted thread. Avoid per-mesh thread creation and duplicate work for submeshes.
3. Prototype compute skinning only after quantifying GPU headroom. Upload immutable source vertices once and joint palettes per changed pose; generate a shared stream for geometry, shadows and GI. Use frame-safe current/previous buffers and explicit compute-to-vertex synchronization. Preserve pause, camera-cut, reappearance and topology-change history rules.
4. Keep the CPU reference/fallback. Design conservative bounds without synchronous GPU readback; validate any joint-bound scheme for blended weights and transformed scale. Use stable resource/pose identity with explicit lifetime and revision invalidation.

Gate: reduced critical-path frame time, not just reduced main-thread time. GPU skinning can regress this scene when the GPU is saturated. Validate skinned shadows, normals and temporal reconstruction against the reference.

## Phase 3 — secondary effects and cache efficiency

- **VSM:** the delayed sample has 143 hits, eight dirty pages, five updated and three deferred, with 840,579 triangles and no overflow. Inspect deformation-driven invalidation, caster/page pairing and upload capacity before changing budgets. The 36.84 MB allocation statistic needs lifetime clarification; it does not prove per-frame allocation churn. Preserve caster coverage and avoid lengthening visible stale-shadow intervals.
- **VCT GI:** split voxelisation, world-cache updates, cone tracing and temporal resolve. Improve dirty-region handling and resident data reuse. Preserve emissive/dynamic response, cascade coverage and current update cadence. The preset already uses world caching, two cones and a command budget.
- **SSAO:** already half resolution with 16 samples and temporal resolve. Optimise reconstruction/data access and redundant transitions before considering fewer samples.
- **Fog/clouds:** preserve the current 32 fog steps, shadow stride two, cloud scale 0.5, 24 primary and three light steps. Investigate provably empty-space rejection and redundant evaluations only after higher priorities.
- **Upscaler/output:** verify actual dimensions and input preparation, eliminate demonstrated redundant copies/transitions. BasicRenderer already suppresses standalone TAA when a temporal upscaler is requested; the preset containing TAA does not establish double AA. Validate fallback behaviour if upscaler evaluation fails.

## Architecture and acceptance

Implement each measured improvement in a small reviewable change, with an independent before/after result. Use existing renderer/RHI ownership and RAII, explicit resource access/barriers, frame-safe histories and backend capability checks. Avoid SSS-specific branches in engine code, global mutable caches, pointer-only lifetime assumptions, per-frame allocation churn and broad refactoring unrelated to measured cost. Preserve project settings by default.

Use the existing Vulkan skinning, Vulkan/OpenGL SSR, VSM and temporal-motion tests as appropriate (`tests/CMakeLists.txt`). Extend them only for meaningful new edge cases: VSM page boundaries, thin SSR occluders, reflected/degenerate skin transforms, and history reset/lifetime transitions. Build affected targets on supported backends and run relevant tests; synthetic tests supplement the real SSS benchmark.

For each change, render deterministic reference and candidate image sequences using identical camera, animation, exposure and warm-up. Report image differences, inspect silhouettes/contact shadows/reflections, and review motion for shimmer, ghosting and disocclusion failures. Exact transformations should match within justified numerical tolerance. Reconstruction changes require explicit visual acceptance, not merely an average image score. Record CPU/GPU median and tail times, memory and allocation changes. Reject improvements smaller than run-to-run noise or those that introduce visual regressions.

Deliverable sequence: baseline/instrumentation → measured geometry improvement → SSR and CPU deformation improvements → secondary effects only if required → full SSS benchmark and visual comparison. Re-rank after each stage; savings do not add linearly across CPU and GPU.

## Source locations

- `engine/render/src/BasicRenderer.cpp`: geometry scope near 1799; post scope near 2029; temporal boundary near 2198; SSR trace/resolve near 2320.
- `engine/render/shaders/BasicLit.slang`: VSM tent near 319–380 and surface evaluation near 741.
- `engine/render/shaders/SSR.slang`: reconstruction near 74; 16-ray integration near 132.
- `engine/render/src/RhiSceneRenderer.cpp`: shared deformation/cache/history and bounds near 342–397.
- `engine/render/src/RhiSkinning.cpp`: reference deformation and normal/tangent handling.
- `D:/PlutoProjects/SSS/Assets/PostProcessing/main.plutopostprocess`: current editor preset.
- `D:/PlutoProjects/SSS/Assets/Scenes/Main.plutoscene`: VSM light and atmospheric settings.

Existing optimisation documents include historical OpenGL work and a different ShadowSouls capture; their timing results are not evidence of current SSS performance.
