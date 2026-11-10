# Volumetric fog froxels

Volumetric fog used to evaluate its lighting at every step of every pixel's march. Each step filtered the directional shadow (the virtual shadow maps or cascades), took two noise-volume octaves for the density variation, and read the cloud shadow map. At the default 16 steps on a half-resolution trace, that is about 8 million shadow and noise evaluations a frame at 1080p.

The froxel volume evaluates that lighting once per froxel instead, and every pixel's march reads it back:

- A froxel is a cell of a grid over the view (16-pixel tiles) and over distance along each tile's ray (64 slices).
- At 1080p the grid is 120 x 68 x 64, about half a million evaluations.
- Temporal reuse converges each froxel to its average and removes the jitter's noise.

On Verdant (1280 x 720, frozen view), the fog went from 6.9 ms to 2.0 ms, of which 0.57 ms is the injection. The scene's GPU time fell from 74.0 to 67.4 ms. The captures differ by 50.4 dB PSNR, with no pixel off by more than 48 levels.

With the camera travelling at 30 m/s, the fog went from 6.5 to 1.9 ms and the scene from 82.6 to 77.8 ms, with no recording failures.

## What the volume holds

Each froxel stores what the march would evaluate at a cell there (`evaluateFogCell` in `VolumetricFog.slang`):

- **rgb:** the light the medium scatters toward the camera: sun times shadow visibility times phase, plus ambient and sky, under the cloud occlusion.
- **alpha:** the density variation (the noise), faded toward the mean with shadow detail.

The march keeps its exact analytic transmittance per step (`heightFogOpticalDepth`), its analytic tail beyond the detail distance, the horizon haze, ocean depth and Max Opacity. Only the per-step lighting and density lookups change. Extinction does not depend on the froxel resolution, so every existing fog check (extinction, haze, sky, cascade coverage, cloud occlusion and shafts, reduced-resolution reconstruction) passes unchanged through the froxel path.

## Layout

- **Slices:** spaced quadratically over the fog's Shadow Detail Distance D. Slice k covers ray distances D (k/N)² to D ((k+1)/N)², so slices are finer near the camera.
- **Atlas:** the volume is a 2D atlas with the slices side by side. An ordinary fragment pass writes it, and the march samples it with explicit-LOD fetches: bilinear within a slice, linear between the two nearest slices, with columns clamped so filtering never crosses into a neighbouring slice. A compute pass writing a 3D image was not used, because the shared fog lighting code samples shadow maps with implicit derivatives, which compute shaders lack.
- **Bindings:** fog has only a depth input, so the froxel pipelines use the free slots 3 (this frame's volume) and 4 (last frame's).

## Temporal reuse

Each frame lights every froxel at one jittered point: Halton (2, 3, 5) over the froxel, on a 16-frame cycle. It then blends that sample 1:9 with the previous frame's volume, reprojected to the froxel centre's position in the previous view.

History is discarded, and that frame lights the froxel centres instead, when any of these holds (`FogFroxelHistory`):

- the grid changes;
- a fog parameter changes (except the noise's wind drift, which converges through the history);
- a frame is skipped;
- the lighting steps by more than 5%, or the sun turns by more than 2°.

Gradual lighting changes, such as a moving sun or drifting clouds, converge through the history with a lag of about ten frames.

## Code

| Piece | Role |
| --- | --- |
| `include/PlutoGE/render/FogFroxels.h` | Pure policy: settings, grid, slice spacing, jitter, history validity. CPU-tested (`PlutoGEFogFroxelTests`). |
| `src/atmosphere/FogFroxelVolume.*` | The two alternating atlases and frame-to-frame state; plans each frame's volume. |
| `shaders/FogFroxels.slang` | Grid mapping, atlas sampling, reprojection and the injection. |
| `shaders/VolumetricFog.slang` | `fogRayLighting` and `evaluateFogCell`, shared by the injection and the per-step march. Entry points `froxelMain` (injection), `traceFroxelMain` and `fragmentFroxelMain` (marches reading the volume). |
| `BasicRenderer` | Plans the volume, records the injection with the fog's lighting bindings, and selects the froxel march. `SetFogFroxels` replaces the settings. |

`PLUTO_FOG_FROXELS=0`, or `SetFogFroxels({.enabled = false})`, restores the per-step march. Without the froxel shaders, the renderer uses it too.

## Limits

- **Lateral resolution:** a shadow edge across the view softens over about half a tile, roughly 0.5° at 1080p. Shafts narrower than a tile lose contrast. `CheckFogFroxels` matches the per-step reference at the angular density 16-pixel tiles have at 1080p. Smaller tiles sharpen shafts at about the square of the cost.
- **One volume per frame:** a second fog effect, or a second camera, in the same frame marches per step, so it does not disturb the first's history.
- **Dynamic resolution:** the grid follows the view size, so a resolution change starts the volume afresh for one frame.
