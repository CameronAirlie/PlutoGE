# Screen-space global illumination (SSGI)

**Screen Space Global Illumination** adds diffuse light bounced from what is on screen: coloured walls tint the floor
beside them, emissive panels light their surroundings, and corners pick up the colour of nearby surfaces. It relights
only the part of the hemisphere its rays find blocked. Rays that miss, or leave the screen, keep the ambient light
already there (sky, reflection captures, baked probes or [DDGI](DDGI.md)), so it layers on any of them. That is
also why it adds nothing under an open sky. The Vulkan and OpenGL RHI renderers use the technique described here; the
legacy OpenGL renderer keeps its older implementation.

## Using it

Cameras created with default effects already carry a disabled SSGI. Enable it, or add **SSGI** in the camera's
post-processing stack.

| Control | RHI meaning |
| --- | --- |
| Intensity | Strength of the replacement: 1 swaps the occluded share of ambient light for the gathered bounce light |
| Ray Distance | World distance a ray marches (default 4) |
| Thickness | Depth below a surface still counted as a hit, in world units |
| Samples | Rays per pixel, 1 to 4 (default 2) |
| Steps | Depth samples per ray (default 16) |
| Blur Radius | Edge-aware denoise passes, 0 to 3 (default 2) |
| Temporal Blend | Share of accumulated history kept each frame |
| History Depth / Normal Threshold | Disocclusion tests for reusing history |
| Half Resolution | Trace at half resolution in each axis (default) |
| Indirect Only | Shows the diffuse indirect light instead of the scene |

**Step Size**, **Depth Weight** and **Normal Weight** apply only to the legacy OpenGL renderer.

## How it works

`ScreenSpaceGlobalIllumination` runs at the surface guide's resolution (`SSGI.slang`):

1. **Trace** (compute). Each pixel sends cosine-weighted rays, rotated per pixel and per frame (R2 sequence), and
   marches them against the depth buffer with SSR's quadratic stepping and three refinement steps. A hit must lie
   within **Thickness** of the surface and face the ray; it gathers the lit scene colour, faded towards the screen
   edges. The output is the mean gathered radiance over all rays and the fraction of rays that hit.
2. **Denoise.** One edge-aware 3 x 3 a-trous pass before accumulation, so the temporal clamp sees signal rather than
   single-ray noise. Further passes at strides 2 and 4 follow it.
3. **Temporal.** History is reprojected along surface motion, rejected where the previous guide disagrees in depth or
   normal, clamped to the current neighbourhood and blended.
4. **Composite** (full resolution). A guided bilateral upsample, then

   `indirect' = indirect + intensity * (response * gathered - hitFraction * indirect)`

   where `indirect` is the diffuse ambient term the pixel currently holds (the geometry pass's, or DDGI's) and
   `response` is the surface's diffuse response. Scene colour moves by the same amount, and the new indirect term is
   what SSAO later attenuates. Unlit and glass pixels are untouched.

## Cost and limitations

The [profiler](PROFILER.md) shows **RHI SSGI / Trace**, **Denoise** and **Composite**. See
[the DDGI measurements](DDGI.md#measurements) for timings beside DDGI's.

Only what is on screen can bounce light, so light from off-screen or hidden surfaces is missing, and it appears or
disappears as those surfaces enter or leave the view. DDGI covers that; SSGI adds the contact-scale detail probes are
too coarse for.

## Validation

`PlutoGEVulkanSsgiTests` and `PlutoGEOpenGLSsgiTests` check bounce light from an emitter onto the floor beside it, an
unchanged image when every ray misses (open ground under uniform ambient light), and both debug views.
`PlutoGEVulkanSsaoTests`/`PlutoGEOpenGLSsaoTests` check that SSAO attenuates SSGI's light but not direct light or
emission.
