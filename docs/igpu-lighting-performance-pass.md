# Integrated-GPU lighting pass — 2026-10-27

The ThirdPersonProject settlement ran at about 83 FPS (12.0 ms/frame) at
1280×720 on the Low preset, on an AMD Ryzen 7 5700G (Radeon Vega 8 iGPU,
Vulkan). The CPU spent most of each frame waiting for the GPU. The opaque
geometry pass took 6.9 ms of the 10.5 ms GPU frame, and the shading
diagnostics showed lighting was about 90% of it: material-only shading costs
0.6 ms.

This pass reformulates the three expensive lighting paths so they do the same
work with fewer dependent, branchy texture reads. Images match the original to
within float rounding. The settlement now runs at 128 FPS (7.8 ms/frame) on
Low.

## Where the time went

Shading diagnostics (`--frozen-render`, opaque pass, before the pass):

| Bypass | Saves |
| --- | --- |
| Point lights (one shadowed fire light, range ≈ 22 m) | 3.0 ms |
| Directional shadows | 2.4 ms |
| Sky lighting | 1.75 ms |
| Detail textures, captures, probe grid | < 0.1 ms each |

These costs came from latency, not arithmetic: on an iGPU with low occupancy,
each texture read that sits behind a per-tap branch or a dynamic loop bound is
waited for separately. Each change below makes a filter's reads independent so
they issue together.

## Changes

All three keep the original code as the general path and add an exact fast
path for the common case.

### Point shadows (BasicLit `evaluatePointShadow`)

Each of the nine taps re-derived its cube face, intersected its ray with the
receiver plane and multiplied by a dynamically indexed face matrix. Within one
face the cube projection is a perspective projection from the light, so the
receiver plane's projected depth is affine in the face's texel coordinates, and
so is the texel ray. When the 3×3 footprint lies inside one face, the shader
solves depth and ray exactly at the centre texel and one neighbour per axis.
Each tap is then one multiply-add plus an independent atlas read. Footprints
that straddle a cube edge use the original reprojecting loop.

Surfaces facing away from the light (`nl <= 0`) skip the shadow lookup: their
standard lighting is zero. Custom-lit shader graphs still receive visibility.

### Cascaded directional shadows (BasicLit `evaluateShadowCascade`)

The tent filter looped up to 5×5 with per-pixel `break`s, and every tap chose
among four cascade textures and checked for a map edge. When the whole tent is
resident and needs at most three bilinear taps per axis (softness ≤ 2; the
default is 1.5), `filterResidentShadowTent` now runs a fixed, unrolled 3×3 of
gathers from one map, selected once per pixel. Taps past the tent add zero
weight. Hard shadows (one tap), wider tents, footprints within a texel of the
map edge and the `scalar-shadows` diagnostic keep the original loop.

A single fixed tap count measured faster (4.69 ms) than specialising per count
with a Slang generic (4.93 ms; 5.15 ms with two specialisations).

### Sky specular (PhysicalSkyEnvironment, SkyQuadrature, SkyEnvironmentConfig.h)

Each roughness level was filtered by hand: four point loads of an RGBA32F
atlas, each with an inlined cube-edge fix-up, at two levels per pixel. The
atlas now stores every face of every level with a one-texel gutter. The
quadrature pass fills the gutter with the texel that a bilinear tap past that
edge belongs to: the neighbouring face's, chosen exactly as the old fix-up chose
it. A level is then one hardware bilinear read through the linear clamp
(`m_screenSampler`) sampler. The atlas is RGBA16F, which every backend can
filter linearly. The layout macros (`PLUTO_SKY_LEVEL_X`, `PLUTO_SKY_TILE_SIZE`,
…) are shared by C++ and Slang. **Rebuild the native host together with the
shader package:** a host built before this change allocates the old atlas size.

### Benchmark quality tier

`--benchmark-project … --quality=low|medium|high|ultra` applies a project tier
before the scene loads. Frozen renders run no scripts, so previously they
always measured the scene's authored quality, not the tier a game selects.

## Results

Settlement, 1280×720, Vulkan, Radeon Vega 8. Frozen runs: 300 frames after 200
warm-up. In-game: 600 frames after 240 warm-up, scripts running, Low applied by
the game's saved settings.

| Measurement | Before | After |
| --- | ---: | ---: |
| In-game Low: frame mean | 11.99 ms (83 FPS) | 7.81 ms (128 FPS) |
| In-game Low: frame p95 | 14.61 ms | 9.07 ms |
| In-game Low: scene GPU | 10.46 ms | 7.24 ms |
| In-game Low: opaque pass | 6.91 ms | 3.70 ms |
| Frozen Low: opaque pass | 7.01 ms | 4.09 ms |
| Frozen High: opaque pass | 7.84 ms | 5.56 ms |
| Frozen High: frame mean | 23.66 ms | 21.51 ms |

Opaque pass at frozen Low as each change landed: 7.01 → 5.67 (point shadows) →
4.69 (cascade tent) → 4.09 ms (sky atlas).

Final frames against the original (1280×720 = 921,600 pixels): Low differs by a
mean of 0.013 / 255 (38 pixels above 2 / 255, maximum 10); High by 0.017 / 255
(32 pixels above 2 / 255, maximum 7). The differences sit on shadow texel edges
(float rounding of the affine solve) and in sky specular (fp16 storage and
hardware filter weights).

## Rejected experiments

Measured and reverted:

- **Unrolled 3×3 VSM resident tent:** the High opaque pass went from 6.24 ms to
  6.60 ms. The VSM cost sits in level selection and page-table lookups (see
  [rendering-performance.md](rendering-performance.md)).
- **TAA neighbourhood sharing:** reading the 3×3 once for motion dilation and
  clipping, with or without fetching all candidate motion vectors up front.
  1.68 ms → 1.71 ms; the compiler already shares those reads.
- **Unconditional sky irradiance reads** (dropping the zero-weight `continue`):
  slower, 5.67 → 5.91 ms.
- **Sky specular fast path without the edge branches** (still four loads per
  level): no change. The cost was read volume, which the gutter atlas removes.

## Validation

Vulkan and OpenGL passed: sky quadrature (analytic reference ≤ 4 / 255),
particle/point shadows (cube-face acne and cross-face occlusion), geometry
diagnostics, opaque shadow batching, IBL, night sky, glass, transparency,
terrain layers, skinning, and the OpenGL default suite (shadow filtering).
`PlutoGEProjectBenchmarkTests` covers the new option.

These failed both with and without this pass, using the same renderer build
with only these changes stashed: `PlutoGE{Vulkan,OpenGL}VsmOnlyTests` ("Fog
ignored VSM caster visibility"), `PlutoGEVulkanRenderOptimizationTests`
("Stationary parameters were reuploaded…"), and the default
`PlutoGEVulkanRhiTests` ("BasicRenderer reported inconsistent recorded-work
statistics", after its shadow checks pass).

## Reproduction

The runtime loads the manifest's startup scene, so benchmark a copy of the
manifest that starts in the scene of interest (in the project root, so asset
paths resolve):

```powershell
# ThirdPersonProject.bench.plutoproject = the project manifest with
# STARTUP_SCENE project://Scenes/Settlement.plutoscene
PlutoGERuntime --benchmark-project <project>/ThirdPersonProject.bench.plutoproject out/low.csv 300 200 `
    --frozen-render --quality=low --capture=out/low.png
PlutoGERuntime --benchmark-project <project>/ThirdPersonProject.bench.plutoproject out/game.csv 600 240
```

`PLUTOGE_SHADER_ROOT=<dir>` runs a different shader package against the same
executable, which is the quickest way to A/B a shader change. The package must
match the host's C++ layouts (the sky atlas changed in this pass).

## Next candidates

At Low, the remaining GPU frame is about 7 ms: opaque 3.7–4.1 ms (point light
BRDF and shadow about 1.5 ms, directional shadows about 1.0 ms, sky irradiance
about 0.4 ms, materials 0.6 ms), TAA 1.7 ms and other post-processing about
1 ms.

- **TAA (1.7 ms at every tier).** No single part dominates: 3×3 normals
  0.21 ms, history depth loads 0.26 ms, motion dilation 0.23 ms. A cheaper
  edge test (cross instead of 3×3 normals) would change output and needs a
  motion review.
- **VSM directional shadows (2.5 ms on High).** The level-selection item in
  [rendering-performance.md](rendering-performance.md) still applies.
- **High-tier post-processing (14.8 ms):** volumetric fog 3.1 ms, SSR 2.75 ms,
  clouds 2.1 ms, VCT GI 2.0 ms and SSAO 1.7 ms. Players on iGPUs are likely to
  use Low or Medium. Medium already omits GI, SSR and volumetrics.
- **Resolution.** The project ships with `RUNTIME_UPSCALER None` and render scale 1.
  On this class of GPU, rendering at reduced scale with the existing FSR2
  upscaler is the largest remaining lever. It is a quality trade-off and a
  per-project choice.
