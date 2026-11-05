# Native TAA

Native temporal anti-aliasing (`TAAEffect`, `TAA.slang`) resolves jittered HDR
scene colour against a reprojected history before camera optics and tone
mapping. FSR2/DLSS replace it when a temporal upscaler is active
([FSR2.md](FSR2.md)).

## Frame inputs

- **Jitter.** `RhiSceneRenderer` advances a 16-phase Halton(2, 3) sequence
  over `[-s/2, s/2]²` pixels (`s` = Jitter Strength) and stores it in lane 2 in
  UV units. `BasicRenderer` offsets `SV_Position` by it; motion vectors use the
  unjittered matrices, so history reprojects by geometric motion only.
- **Texture LOD bias.** With native TAA active the material sampler uses
  `NativeTaaMipLodBias(s)` = `-0.5·min(s, 1)`. Accumulating jittered samples
  integrates a pixel-sized box over textures that mip selection already
  filtered for that footprint; half a level restores the detail that second
  filter removes. Upscalers use `UpscalerMipLodBias` instead. Both live in
  `engine/render/src/TemporalSampling.h`, and `BasicRenderer::Render` applies
  the result each frame (the sampler is recreated only on change).

## Resolve

- **Current-sample weight.** Equal weights converge to a one-pixel box filter.
  `TaaCurrentSampleWeight` weights each frame by `exp(-2.29·|jitter|²)` (the
  Gaussian fit to Blackman-Harris), normalised to a mean of one over the jitter
  distribution, and `BasicRenderer` writes it to lane 3.y. The shader applies it
  to the requested history weight before validity, so centred samples count
  more while rejected history still falls back to the current frame, and the
  average convergence rate is unchanged.
- **History filter.** History is always sampled bicubically (Catmull-Rom):
  Balanced uses 5 taps (corners dropped and renormalised), High 9. Bilinear
  history re-filters every reprojected frame and compounds into blur under
  motion.
- **Rejection.** Variance-clipped neighbourhood bounds in a reversible
  tone-mapped space, a depth interval over the 2×2 history footprint, camera
  layers, and a lighting check reject stale history; see the comments in
  `TAA.slang`.

## Sharpening

The Sharpening setting drives the renderer's RCAS pass
(`UpscalerSharpening`, the same pass SDK upscalers use) on the resolved
output. It never touches the history: sharpened history would feed back and
compound every frame. 0 disables the pass, and 1 is the strongest setting.
