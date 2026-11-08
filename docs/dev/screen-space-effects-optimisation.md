# Screen-space effects: shared guide, stochastic SSR and cheaper SSAO

SSR and SSAO were the two most expensive screen-space effects on the iGPU target (Radeon Vega 8, see
[igpu-lighting-performance-pass.md](igpu-lighting-performance-pass.md)). This pass restructures both after the
designs other engines use (Frostbite's and AMD FidelityFX's stochastic SSR, Intel's XeGTAO) and moves them out of
`BasicRenderer` into a self-contained module.

## Architecture

`engine/render/src/screenspace/` holds three passes that depend only on the RHI and `ScreenSpacePass.h`, never on
`BasicRenderer`. Each takes an explicit `Settings` struct, which the renderer derives from the effect's lanes in
`BasicRenderer::RenderSsao` and `RenderSsr`.

| Class | Shaders | Role |
| --- | --- | --- |
| `ScreenSpaceGuide` | `ScreenSpaceGuide.slang`, `ScreenSpaceGuideHierarchy.slang`, `ScreenSpaceGuideCommon.slang` | One compute dispatch reads the G-buffer once. It writes an RGBA32F guide (linear depth, octahedral world normal, roughness) at full or half resolution and, on request, a five-level linear depth hierarchy reduced in group-shared memory. It keeps the previous frame's guide for disocclusion tests. |
| `ScreenSpaceAmbientOcclusion` | `SSAO.slang`, `SSAOHorizonSampling.slang`, `SSAOResolve.slang`, `SSAOUpsample.slang` | Raw AO, edge-aware denoise, temporal resolve, then a full-resolution upsample and composite. |
| `ScreenSpaceReflections` | `SSR.slang` (+ stage variants), `SSRCommon.slang`, `SSRStochastic.slang` | The **Stochastic** (default) and **Reference** techniques. |

`ScreenSpacePass.h` provides what the passes share: `ScreenSpaceView` (camera, G-buffer and frame state captured once
per frame), a per-frame uniform pool (uniform buffers hold one value per frame, so each draw takes its own),
size-tracked targets and pipeline/draw helpers.

The renderer keeps one guide per resolution divisor. The first effect that needs a guide in a frame builds it; any
depth hierarchy SSAO needs is merged into that build (`PrepareScreenSpaceGuide`), so SSR and SSAO share one dispatch.

### Representative pixels

A half-resolution guide texel describes one real G-buffer pixel, never a blend: the pixel under the texel centre,
which is `2i + 1` for even extents and `2i` for odd ones (`guideRepresentativePixel`). SSAO and SSR reconstruct
positions there. Integer division was avoided deliberately: GCN emulates it, and it ran per tap, which cost 0.1 ms
in the SSAO composite alone.

### Extending

* A new screen-space effect takes a `ScreenSpaceGuide &` and reads the guide instead of the G-buffer. If it needs
  another hierarchy reduction (nearest depth for a hierarchical march, for example), add it as a guide variant
  next to `ScreenSpaceGuideHierarchy`.
* A new SSR technique adds a `Technique` value, its stages in `RecordXxx`, and a branch in `Resolve`; the lane
  layout in `SSR.slang` is shared.
* Effect lanes stay owned by the effect adapter (`RhiPostProcessAdapter.cpp`); the passes see named settings.

## SSAO

| Stage | Change |
| --- | --- |
| Depth hierarchy | One dispatch for five levels, starting at half resolution (previously six dispatches with a barrier between each, starting at full resolution). |
| Raw AO | Receiver depth, normal and neighbours come from the guide. GTAO uses XeGTAO's fast `acos`, a closed-form empty-hemisphere integral and the normal angle's sine and cosine once per slice. Hierarchy taps reconstruct at the centre of the pixels they reduce, which also fixed false height on odd extents. The pass packs four depth/normal edges beside visibility. |
| Denoise | XeGTAO-style 3 x 3 edge-aware passes that read only the raw target. **Blur Radius** is now the number of passes (0 to 3). |
| Temporal resolve | Combined with the last denoise pass. Its clamp range spans the edge-connected neighbourhood, so temporal accumulation also works with blur off. |
| Composite | Pixel loads instead of half-rate RGBA16F filtering. Unoccluded pixels skip the indirect inputs. |

The sample budget maps to slices x steps per side: 32 and 18 give 3 x 3, 16 gives 2 x 3, 8 gives XeGTAO's medium
2 x 2, below that 1 x 2.

## Stochastic SSR

1. **Trace** (compute): one GGX visible-normal ray per half-resolution pixel, rotated per frame (R2 sequence),
   marched with the reference technique's march, refinement and hit tests (`SSRCommon.slang`). The top 10% of the
   lobe's tail is not sampled: those rare far-off-lobe rays flip between hit and miss and dominated the residual
   noise (Frostbite trims more aggressively). Output: direction, hit distance, validity.
2. **Ray reuse**: each pixel weights its own ray and four neighbours' rays by its BRDF over their sampling pdf.
   The neighbours are the orthogonal four on even frames and the diagonals on odd ones, at twice the distance for
   rough lobes, and only from the same surface. The weights form the reference technique's ratio estimator, so a
   constant environment still matches its capture exactly and misses keep the fallback. For a pixel's own ray the
   weight reduces to the reference path's `G1(L)`.
3. **Temporal**: history is reprojected along surface motion and along reflection parallax (the virtual image at
   the hit distance), keeping whichever is closer to the current neighbourhood (FidelityFX SSSR). Disocclusion is
   tested against the previous guide. History is clipped to the neighbourhood's mean +/- 1.25 standard deviations
   and accumulates up to 8 (mirror) to 16 (rough) frames.
4. **Composite**: full-resolution bilateral upsample against the guide (plane, normal and roughness agreement),
   faded by roughness as before.

The **Reference** technique (16 rays, separable filter, stage variants) is unchanged. Its SPIR-V is byte-identical
to before the shared functions moved into `SSRCommon.slang`. Select it with the effect's **Technique** parameter
(lane 3.y = 1); the full-resolution comparison path (lane 4.w) always uses it.

## Results

Radeon Vega 8 (Ryzen 7 5700G), Vulkan, Release. Milliseconds of GPU time; effect totals include the shared guide.

ThirdPersonProject `Settlement`, High tier, frozen, 853 x 480 internal (FSR2 Quality to 720p):

| Scope | Before | After |
| --- | ---: | ---: |
| SSR | 1.23 | 0.34 |
| SSAO | 0.79 | 0.66 |
| Scene GPU | 13.73 | 12.98 |

(Means of two consecutive runs; single runs of the same build varied by up to 0.7 ms of scene time on this iGPU.)

Synthetic SSR capture workload (`--ssr-capture-performance`: 1690 x 934, 48 steps, five refinements, rough
metallic floor):

| Half-resolution SSR | Total |
| --- | ---: |
| Reference | 21.52 (trace 18.10, filters 2.70, resolve 0.71) |
| Stochastic | 3.50 (trace 1.37, ray reuse 0.67, temporal 0.35, composite 0.75, guide 0.36) |

SSAO by sample budget (`--ssao-performance`, horizon estimator, half resolution, two denoise passes):

| Budget | 853 x 480 total (raw) | 1690 x 934 total (raw) |
| ---: | ---: | ---: |
| 32 | 0.65 (0.27) | 2.45 (0.99) |
| 16 | 0.60 (0.20) | 2.16 (0.72) |
| 8 | 0.57 (0.17) | 2.03 (0.58) |

ThirdPersonProject's `Main.plutopostprocess` sets **Samples** to 32. Eight samples would save a further 0.1 ms at
853 x 480; that is a per-project quality choice and was not changed.

Static and panning (`--camera-pan=8`) captures of Settlement differ from the previous renderer by a mean of 0.17 and
0.13 levels (8-bit), concentrated at AO contact edges and reflection silhouettes, with no visible ghosting.

## Measured and rejected

* **Tile classification with an indirect trace dispatch.** A classification pass listed 8 x 8 tiles with any
  reflective pixel for `DispatchIndirect`. With one ray per pixel, pixels without a ray already leave the trace
  almost at once, so classification cost more than it saved: Settlement 0.098 ms (classify 0.066 + trace 0.032)
  against 0.080 ms traced directly; capture workload 3.80 against 3.72 ms. The pass and the RHI addition it needed
  were removed.
* **Hierarchical (min-depth) trace.** Not implemented again. The trace is now 0.03 to 0.08 ms in Settlement (9 to
  24% of SSR), so even halving it could not save a measurable amount there; the earlier 16-ray experiment is in
  [ssr-stage-optimisation.md](ssr-stage-optimisation.md). It remains a candidate for heavily reflective scenes,
  where the capture workload's trace is about a third of SSR.
* **AO applied in the lighting pass.** This would remove the 0.18 ms composite, but on High SSR already forces the
  G-buffer layout that writes the indirect target, so that write stays. It would add bilateral AO taps to the lit
  shader, require a complete depth prepass whenever SSAO is on (today it covers eligible draws only), derive normals
  from depth, and make SSGI and VCT GI sample AO themselves to keep occluding post-process GI. The net gain is at most
  about 1% of the frame, so the composite was made cheaper instead.

## Tests and reproduction

* `PlutoGE(Vulkan|OpenGL)SsrTests` run the behavioural SSR checks for both techniques. Reference keeps every exact
  check, including byte-identical stage specialisation. Stochastic renders 24 frames per measurement so its history
  settles. Its AO-interaction check compares mean and signed mean (residual sampling noise differs per frame); its
  noise check passes with a variation ratio of about 0.2, against a bound of 0.8.
* `PlutoGE(Vulkan|OpenGL)SsaoTests` cover both estimators, blur 0 to 4, full and half resolution, odd extents,
  coplanar receivers and GI/direct-light composition.
* `PlutoGEVulkanRhiTests --ssr-capture-performance` and `--ssao-performance` print per-stage GPU timings.
* Game measurements: `PlutoGERuntime --benchmark-project <manifest copy starting in Settlement> out.csv 300 200
  --frozen-render --quality=high` writes `out.gpu.csv` with every scope.
