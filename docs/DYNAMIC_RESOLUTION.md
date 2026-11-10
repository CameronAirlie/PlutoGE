# Dynamic resolution

The scene can render below its normal size when the GPU cannot hold a frame rate, and return to full size when it
can. On a pixel-bound GPU (the integrated GPUs this engine targets) a frame's cost follows the pixels it shades, so
this holds a frame rate with the smallest loss of quality.

## Enabling it

- **Project:** `RUNTIME_DYNAMIC_RESOLUTION <0|1> <target FPS> <minimum scale>` in the project file, for example
  `RUNTIME_DYNAMIC_RESOLUTION 1 60 0.5`. Off by default.
- **Code:** `RhiRenderService::SetDynamicResolution` or `RhiSceneRenderer::SetDynamicResolution` with a
  `DynamicResolutionSettings`; `EngineConfig::dynamicResolution` at startup.
- **Benchmarks:** `PlutoGERuntime --benchmark-project <project> out.csv <frames> <warmup> --dynamic-resolution=60`.
  The CSV's `internal_width` and `internal_height` columns show the size each frame rendered at.

## What scales

The scale multiplies both axes of the size the view would otherwise render at: the output size, or the temporal
upscaler's quality-mode size. A scale of 1 is never exceeded.

- **With FSR2 or DLSS,** the upscaler reconstructs the output from the smaller image. Its context is created for the
  largest render size, so it keeps its history across scale changes; only render-resolution histories (TAA, SSAO,
  SSR, SSGI guides) restart.
- **Without an upscaler,** post-processing runs at the render size and the display pass stretches it over the output.
  Every temporal history restarts on a change, as on a resize.

Render targets are rebuilt when the scale changes level, so the controller changes level rarely.

## The controller

`DynamicResolutionController` (`DynamicResolution.h`) watches completed GPU times of the renderer's own submissions,
matched to the scale each frame was prepared at:

- Cost is modelled as fixed plus per-pixel. The per-pixel share is learnt from frames at two scales; until then all
  cost is assumed to scale with pixels.
- After `lowerAfter` (4) frames over the target, it jumps straight to the largest level predicted to fit.
- After `raiseAfter` (45) frames where the next level up is predicted to fit within `raiseHeadroom` (85%) of the
  target, it raises one level.
- Every change starts a `cooldown` (30 frames). Levels are `scaleStep` (0.05) apart, and render sizes are rounded
  to even pixels.

The decision is behind `IDynamicResolutionPolicy`; `RhiSceneRenderer::SetDynamicResolutionPolicy` replaces it (for
example with a policy that also considers CPU time).

GPU times come from the device's frame timestamps. Vulkan publishes them by default. OpenGL does not, so there the
scale stays at its maximum.

## Measured (Wind Fields, 1920x1080, Vega 8, default preset)

| Configuration | Render size | Frame (ms) | Scene GPU (ms) |
| --- | --- | --- | --- |
| Native | 1920x1080 | 23.3 | 22.7 |
| Native, target 60 FPS | 1344x756 (0.7) | 14.8 | 14.3 |
| Native, target 90 FPS (floor 0.5) | 960x540 | 10.5 | 10.0 |
| FSR2 Quality | 1280x720 | 21.1 | 20.5 |
| FSR2 Quality, target 90 FPS (floor 0.5) | 640x360 | 13.7 | 13.2 |

## Checks

- `PlutoGEDynamicResolutionTests`: settling under a simulated fixed + per-pixel cost with two frames of latency,
  returning to full scale when load drops, no oscillation under noise near the target, the minimum scale and render
  extent rounding.
- `PlutoGEVulkanRenderScaleTests`, `PlutoGEOpenGLRenderScaleTests` (`--render-scale`): a render size below the output
  without an upscaler fills the whole output, through auto exposure and tone mapping. Auto exposure used to write
  into output-sized targets, which left the image in the top-left corner.
