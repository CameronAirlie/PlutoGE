# SSS geometry cost investigation

## Implemented optimisations and validation

Implemented the sky-sample precomputation and omission of the unused diagnostic attachment. The investigation below records the earlier baseline; VSM filtering and draw ordering remain unchanged.

- `SkyQuadrature.slang` evaluates the existing atmospheric shader at the same 14 directions into a 14 × 1 RGBA32F texture. BasicRenderer owns its pipeline, buffer and texture through the existing RHI resource wrappers, generates it before geometry each sky-enabled frame, and samples with nearest filtering. No CPU readback, atmosphere-model duplication or history cache is introduced.
- BasicLit's diffuse sky integration retains the existing per-normal weights, accumulation order and denominator. Zero-weight cached samples skip their texture fetch. Specular evaluation stays unchanged. The original calculation remains the fallback when the quadrature shader package is absent and is used by the image-reference tests. Ocean/fog consumers of the shared helper retain their existing evaluation path.
- Normal views use five geometry attachments instead of six. Diagnostic views select corresponding opaque/instanced/outline pipelines and lazily allocate the diagnostic target. The display pass receives a valid fallback texture when that target has never been needed. Resizing invalidates the diagnostic target; ordinary views do not write it even after visiting a debug view.
- Logical colour-output bytes drop from 36 to 28 per pixel in normal views. This is not a proportional frame-time saving.

The final `msvc-nvidia` RelWithDebInfo editor and Vulkan/OpenGL test executables built successfully. All 13 selected tests passed, covering both RHI suites, sky comparison, fog, VSM-only views, outlines, temporal motion and Vulkan skinning. The sky tests compare 12 changing sun/exposure/material states over varying normals: **maximum 8-bit output difference was 0/255 on both backends**. A debug-view round trip also preserves the normal-view image exactly. This establishes tested output equivalence, not universal HDR bitwise equivalence or a full SSS motion comparison. `git diff --check` passed.

### Final synthetic performance

Three successful Vulkan benchmark runs, each with 64 warm-up and 64 measured frames per variant. At 1005 × 594, median of the per-run means:

| Geometry configuration | Before | After |
| --- | ---: | ---: |
| Sky only, diagnostic output retained | 1.077 ms | 0.722 ms |
| Sky + VSM, diagnostic output retained | 2.247 ms | 1.929 ms |
| Sky + VSM, normal view without diagnostic output | Not separately recorded | 1.888 ms |

Sky precomputation saves approximately 0.318 ms (14%) in the matched sky+VSM geometry case. Removing diagnostic output saves a further approximately 0.041 ms within the new build. The matched diagnostic-view **total scene GPU** time, including the new sky-generation pass, falls from approximately 2.416 ms to 2.098 ms. These are synthetic measurements on the same local setup, not an SSS FPS result; full-scene before/after comparison remains necessary. Do not compare total scene time between diagnostic and normal output as if only geometry differed, because the display shader also changes mode.

Artifacts:

- Editor: `out/build/msvc-nvidia/editor/RelWithDebInfo/PlutoGEEditor.exe`.
- Final build: `out/build/sky-optimisation-final-build.log`.
- Tests: `out/build/sky-optimisation-final-tests.log`.
- Final benchmarks: `out/build/sky-optimisation-final-performance1.log` through `performance3.log`.

Next priorities remain measuring actual SSS overdraw/material cost and evaluating a fidelity-preserving VSM filter change. No shadow samples, reflection rays, scene quality settings or object ordering were reduced in this implementation.

## Original investigation outcome

Physical-sky surface illumination and virtual-shadow receiver filtering are both demonstrated GPU costs. The best first fidelity-preserving candidate is precomputing the 14 fixed-direction sky radiance evaluations used by diffuse illumination. Keep the existing directions, normal-dependent weights, accumulation order and normalization. This is a recommendation, not an implemented rendering change.

The SSS captures report geometry at 10.96 ms for a 1005 × 594 viewport and 6.73 ms for 603 × 346. They are asynchronous observations from different views/resolutions, not an A/B benchmark. The synthetic experiment below establishes shader-path costs but does not explain all of SSS's geometry time.

## Measured diagnostic

Extended `tests/VirtualShadowPerformanceChecks.h`, using the real BasicRenderer and Vulkan shaders. Built the `msvc-nvidia` RelWithDebInfo Vulkan test executable and ran `--vsm-performance` three times successfully. Each variant uses 64 warm-up and 64 measured frames, with the existing static receiver/caster fixture and converged page cache. No post-process effects or temporal upscaling are enabled. A shadow diagnostic output pass remains enabled in every variant; reported numbers below are the geometry GPU scope, not total scene time.

Median of the three per-run mean geometry times:

| Diagnostic variant | 603 × 346 | 1005 × 594 |
| --- | ---: | ---: |
| No physical sky or shadows | 0.163 ms | 0.438 ms |
| VSM softness 0, no sky | 0.406 ms | 0.957 ms |
| VSM softness 1, no sky | 0.630 ms | 1.512 ms |
| Physical sky, no shadows | 0.442 ms | 1.077 ms |
| Physical sky + VSM softness 1 | 0.926 ms | 2.247 ms |

At the larger size, enabling soft VSM adds approximately 1.074 ms without sky, and adding sky to that VSM case adds approximately 0.735 ms. These are diagnostic differences, not predicted SSS savings. The benchmark has one visible receiver draw with simple material inputs, whereas SSS has 82–87 visible draws, detailed meshes/materials and potentially substantial overdraw. GPU hardware/driver identity was not recorded. Variants run in a fixed order, so this is an initial diagnostic rather than a comprehensive benchmark.

Logs: `out/build/geometry-investigation-build.log` and `out/build/geometry-investigation-run1.log` through `run3.log`. The test process reports Streamline unavailable due to its test-local interposer signature; this benchmark does not use DLSS. Existing VSM cache/budget assertions passed in all three runs. `git diff --check` passed. Only the benchmark and this report were changed.

## What is inside geometry

`engine/render/src/BasicRenderer.cpp`, geometry scope near line 1799:

- Opaque and alpha-tested draws, followed by outline shells.
- Material textures, shader-graph evaluation, direct lighting, physical-sky environment lighting and shadow receiver filtering in `BasicLit.slang`.
- Six colour attachments: HDR colour (8 bytes/pixel), normals (4), material (4), motion (8), albedo (4) and diagnostics (8): 36 logical bytes/pixel before depth, compression, overdraw and other traffic.
- Transparent draws are collected here but rendered later. Glass fragment shading is not charged to this geometry GPU scope.

The separate VSM planning/page timings cover producing shadow data. They do not include the surface shader's sampling/filtering cost. Similarly, the small `RHI Physical Sky` post-effect timing does not include opaque surface environment illumination.

## Findings and next implementation order

### 1. Precompute fixed sky radiance samples

`PhysicalSkyEnvironment.slang::samplePhysicalSkyIrradiance` loops over 14 fixed world-space directions. For each direction it calls `samplePhysicalSkyEnvironment`, whose atmospheric model contains exponentials, powers and phase-function calculations. The surface normal affects the weighting, but not the radiance at those fixed directions. `BasicLit.slang` invokes this per fragment when physical sky is enabled, plus an additional view-dependent environment evaluation for specular lighting.

Produce the 14 radiance values once per sky parameter state (or frame initially) and share them with opaque/water consumers. Prefer a small renderer-owned GPU resource computed from the existing shader model to avoid duplicating the atmospheric equations on CPU. Explicitly order writes before shading; key reuse by all atmospheric inputs and exposure. Keep per-fragment quadrature weighting and its denominator unchanged. Leave view-dependent specular evaluation intact initially.

This does not require a lower-resolution cubemap, fewer integration directions or reduced atmospheric fidelity. Validate across sun angles, day/night, ground-facing normals and sky changes with a numerical reference and animated image comparisons. Measure resource-update/barrier costs as well as geometry savings. Source-level repetition does not by itself establish generated instruction counts; the measured sky-on/off difference is the supporting evidence.

### 2. Optimise VSM filtering without narrowing its footprint

`BasicLit.slang::evaluateVirtualShadow` resolves up to four page mappings, reuses mappings when the footprint lies within one page, then visits each texel in its tent footprint. Softness 1 yields support 2 at the requested level, generally up to 4 × 4 comparisons. Softness 0 still has a support-1 tent, so it is not a zero-work reference.

Measure opportunities to hoist address/weight arithmetic and use gather operations where supported. Preserve each tap's receiver-plane depth correction, page-edge addressing and coarser-level fallback. Do not assume a single hardware comparison sample preserves the current calculation. Page-table reuse already exists; proposing it again would not address the remaining cost.

### 3. Measure overdraw and material complexity in SSS

`Renderer.cpp::CompareRenderCommandKeysImpl` orders by shader, material and mesh rather than front-to-back depth. `BasicRenderer` consumes opaque draws in their supplied order. This permits avoidable fragment shading, but neither this experiment nor the supplied profiler counts measure actual overdraw.

Next SSS capture should include GPU pipeline statistics/overdraw and main-pass triangle counts, with matched camera/internal resolution. Compare material-preserving front-to-back ordering and a coverage-correct depth prepass only after measuring. Keep transparent ordering and temporal object identity stable. The shader-graph interpreter, textures, mesh density and visibility differ from the synthetic receiver and may account for additional cost; none is proven dominant here.

### 4. Remove unused diagnostic attachment work

The 8-byte/pixel diagnostic target is allocated, bound, cleared and written during ordinary geometry. Its sampled use in BasicRenderer is the debug-view pass. Audit external access and every debug consumer, then use a pipeline/attachment variant that omits it when unused. It is 22% of logical colour-output bytes, not 22% of geometry time. Compression and bottlenecks determine the actual saving.

## Original investigation scope limits

No production shader, scene quality or render ordering was changed. No claim is made that these diagnostic deltas add up to SSS's measured geometry time, or that a particular FPS improvement has been achieved. Implement and validate the sky precomputation first, then repeat an identical SSS view and re-rank the remaining work.
