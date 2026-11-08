# SSR: lobe-adaptive rays and trace-resolution filtering

Two changes to the RHI screen-space reflection path. The trace's march, refinement, hit test and resolve
normalisation are unchanged.

## Lobe-adaptive ray count

Every trace pixel used to trace a fixed sixteen GGX rays. On smooth surfaces the lobe is narrower than a pixel at
the rays' reach, so all sixteen march across the same depth texels.

`SSR.slang` now builds the pixel's visible-normal sampler once (`ReflectionLobe`, which also hoists the per-pixel
VNDF setup out of the ray loop). `lobeRayCount` then projects the centre ray and the outermost stratum in four
directions to where the centre ray stops marching (its end, or the screen edge from `screenExit`; the spread
only grows with travel). It traces about one ray per trace pixel of that span, in whole groups of four, between
four and the configured count. Lobes wider than the configured count keep every ray, so their result is exact.
Four rays stay the minimum so sub-pixel hit/miss edges remain antialiased. With fewer than sixteen rays and a
temporal resolve active, the stratification rotates every frame, as the reduced-ray variant already did.

The SSR effect's **Adaptive Rays** parameter (lane 3.x, default on) enables it; off, or an older parameter block,
traces the configured count.

Measuring the span where the centre ray first hits was tried and removed. GGX's outer strata keep mid-roughness
lobes wider than sixteen pixels even at short hit distances, so the extra probe march only added about 1.5%.

## Trace-resolution separable filter

The separable bilateral filter ran its horizontal half at trace resolution and its vertical half at full
resolution while upsampling: up to 33 rows x 2 columns of geometry-aware taps per full-resolution pixel. Both
halves now run at trace resolution (stages 3 and 4; `SSRFilterVertical.slang`), and the full-resolution
stage 2 is a 2x2 bilateral upsample with the same depth, normal and material weights. The cost of the wide
footprint now scales with trace pixels, a quarter of the output.

## Measurements (RTX 4070 SUPER, Vulkan, Release)

`PlutoGEVulkanRhiTests --ssr-capture-performance` (1690 x 934 output, 845 x 467 trace, 48 steps, five
refinements, sixteen rays; adaptive rays off), alternating builds:

| Stage | Before | After |
| --- | ---: | ---: |
| Trace | 2.10 ms | 2.11 ms |
| Horizontal filter | 0.22 ms | 0.29 ms |
| Vertical filter | (in resolve) | 0.14 ms |
| Upsample / resolve | 0.51 ms | 0.04 ms |
| SSR total | 2.83 ms | 2.58 ms |

`PlutoGEVulkanRhiTests --ssr-adaptive-performance` (same size, project settings) traces the floor at each
roughness with sixteen fixed rays, sixteen adaptive, and four:

| Floor roughness | Fixed | Adaptive | Four rays |
| ---: | ---: | ---: | ---: |
| 0.45 | 2.27 ms | 2.28 ms | 0.59 ms |
| 0.20 | 1.95 ms | 1.95 ms | 0.49 ms |
| 0.04 | 0.62 ms | 0.34 ms | 0.16 ms |

Adaptive rays help near-mirror surfaces and are neutral elsewhere. Mid-roughness cost falls only with fewer rays
per pixel (the **Rays per Pixel** parameter), which relies on a temporal resolve (TAA or an upscaler) to
integrate the rotated samples.

## Validation

- `CheckSsrAdaptiveRays` (Vulkan and OpenGL `--ssr-only`): adaptive and fixed rays are identical at roughness 1.
  Mid roughness differs by at most four levels. At roughness 0.04, the mean difference is under 0.03 and under
  0.1% of values change by more than eight levels (isolated pixels on reflected silhouettes).
- The existing SSR quality checks (energy, rough variation, capture replacement, miss preservation) pass on both
  backends. Stage specialisation still matches the generic shader byte-for-byte.
