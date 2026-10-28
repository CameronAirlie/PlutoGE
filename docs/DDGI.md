# Dynamic diffuse global illumination (DDGI)

**Dynamic Diffuse GI (Probes)** lights opaque surfaces with a grid of irradiance probes that follows the camera and
updates continuously from the scene. Unlike [ambient probe grids](ambient-probe-grids.md) nothing is baked: moving
lights, opening doors and changing emissive materials change the bounce light within a few frames. Unlike voxel cone
traced GI, the sky is occluded too, so interiors darken without manual volumes. It works on Vulkan and OpenGL RHI;
the legacy OpenGL renderer skips it.

## Using it

Add **DDGI** to a camera's post-processing stack (Inspector > Camera > Post Processing > Add Effect). Pick a
**Quality**; the inspector shows what it costs under the parameters:

| Quality | Probes per cascade (finest spacing) | Finest cascade / reach of the third | Rays per probe | Probes per frame | Voxels |
| --- | --- | --- | --- | --- | --- |
| Low | 16 x 8 x 16 (3.0) | 45 x 21 x 45 / 405 x 189 x 405 | 64 | 768 | 64 |
| Medium (default) | 24 x 12 x 24 (2.0) | 46 x 22 x 46 / 414 x 198 x 414 | 96 | 1,728 | 64 |
| High | 32 x 16 x 32 (1.5) | 47 x 23 x 47 / 419 x 203 x 419 | 128 | 3,072 | 128 |
| Ultra | 32 x 16 x 32 (1.5) | 47 x 23 x 47 / 419 x 203 x 419 | 192 | 4,096 | 128 |

Every level uses three **Probe Cascades**: nested grids around the camera, each three times the spacing of the one
inside it, so the volume reaches about 400 units with the probe count of three small grids. **Probes Per Frame** is
split 4:2:1 between them, so the finest refreshes every seven to nine frames and the widest about four times less often.
Editing any of those values shows **Custom**. One cascade covers only the finest grid; beyond it surfaces keep their
unoccluded ambient light, which shows as a box of probe lighting around the camera in open levels.

Other controls:

- **Intensity** scales the probe lighting.
- **Hysteresis** (default 0.97) is the share of a probe's history kept per update. Each probe updates only every
  few frames, so lower values average fewer noisy updates and the lighting shimmers; 0.9 roughly doubled the flicker
  in RocketLeg. Changes that double or halve a probe's brightness (a light switched on) use half the hysteresis, so
  large changes still settle in a few updates.
- **Normal Bias** and **View Bias** (in probe spacings) move lookups off the surface so probes behind it are not seen.
  The renderer never uses less than one and a half voxels, which is as sharply as the voxel scene places surfaces.
- **Probe Relocation** moves probes out of walls and away from nearby surfaces, by up to 0.45 spacings.
- **Probe Classification** stops tracing all but 16 rays for probes inside geometry or with no surface within two
  spacings, since nothing samples them. This saves a lot in open areas.
- **Multiple Bounces** (on by default): rays that hit a surface also reflect the volume's own irradiance there. Over a
  few updates this bounces sky light, and every further bounce, through the scene. Without it, interiors only get
  direct and once-bounced light from the voxel scene and look much darker than they should. While it is on, the voxel
  scene's single **Secondary Bounce** is not gathered, since it would count that bounce twice.
- **Brightness Clamp** limits the luminance one ray gathers from the scene, so small, bright emitters do not flicker
  through single rays. Sky light is never clamped.
- **Voxel** settings (**Resolution**, **Cascades**, **Volume Size**, **Update Speed**, **Inject Local Lights**,
  **Local Light Bounce**, **Secondary Bounce**, **Voxelization Budget**) configure the voxel scene the rays march.
  They mean what they do for [voxel cone traced GI](vct-world-cache.md). When the camera also has **Voxel Cone
  Traced GI**, its voxel settings apply to both.
- **Debug View**: **Irradiance** shows the probe lighting alone. **Indirect Lighting** shows the diffuse light the
  surfaces receive. **Probes** draws each probe as a sphere shaded with its irradiance, depth-tested against the scene;
  inactive probes are dark red and probes waiting for their first update purple. **Probe Debug Radius** sizes the
  spheres.

Use DDGI *or* voxel cone traced GI for diffuse bounce, not both: both add bounce light. [SSGI](SSGI.md) combines
well with DDGI, adding contact-scale bounce on top of it. Project Settings > Quality > Global illumination turns DDGI
off with the other GI effects.

## How it works

The frame runs in this order: geometry pass, DDGI, SSGI, SSAO.

1. **Placement.** Each cascade is centred on the camera and snapped to whole cells of its own spacing
   (`DdgiProbeGrid`). A probe's data lives at its world cell modulo the grid size, so scrolling moves no data. Probes
   that enter a cascade lose their history and are traced first. A cascade only scrolls once the camera is more than a
   cell from its centre, so small motion does not discard the edge slabs. All cascades share the atlases: a probe's
   global index is cascade x probes per cascade + its index in the cascade.
2. **Scheduling.** Each frame traces each cascade's share of **Probes Per Frame** (`DdgiProbeScheduler`): new probes
   first, then a sweep that visits every probe once in a scattered order, so refreshes do not travel through the grid
   as a plane. All cascades' probes are traced, relocated, classified and blended in the same dispatches.
3. **Tracing.** A ray source fills a buffer of (radiance, hit distance) per ray (`IDdgiRaySource`). The current
   source, `VoxelDdgiRaySource`, marches near-ray cones through the VCT voxel cascades (voxelized, relit, with one
   secondary bounce). Transmission that leaves the scene picks up the physical sky (or the flat ambient term), exactly
   as the geometry pass lights surfaces. With **Multiple Bounces**, a hit adds the voxel's albedo times the
   volume's irradiance at the hit point from the previous update. Each probe traces 16 fixed rays for relocation and classification, plus
   **Rays Per Probe** spherical-Fibonacci rays rotated randomly at every update.
4. **Probe update** (`DDGIProbeUpdate.slang`). Relocation and classification use the fixed rays. Rays are then blended
   into two octahedral atlases per probe: 6 x 6 texels of irradiance and 14 x 14 texels of hit distance and squared
   distance, both with a one-texel border. Changes beyond twice the ray noise (doubling or halving a probe's
   luminance, above 0.1) converge at half the hysteresis.
5. **Apply** (`DDGIApply.slang`). Each opaque lit pixel interpolates its eight surrounding probes, weighted by
   trilinear position, a smooth backface term and Chebyshev visibility. The result *replaces* the diffuse ambient the
   geometry pass wrote: sky, reflection-capture or baked-grid irradiance times the surface's diffuse response. It fades
   out over the grid's outermost cell. Unlit, glass and transparent surfaces keep their own ambient light.
   A pixel uses the finest cascade that contains it and blends into the next one out over that cascade's outermost
   cell; biases scale with each cascade's spacing.

Irradiance uses the renderer's existing ambient convention (cosine-weighted mean radiance times
`albedo (1 - F)(1 - metallic) / pi`). An unoccluded scene therefore looks as it does without DDGI: the GPU check
measures 0.96 of the flat-ambient brightness on open ground, the difference being ambient Fresnel, which the geometry
pass omits.

### Replacing the ray source

`DdgiVolume` depends only on `IDdgiRaySource`. A hardware ray-query source would implement `Trace` with the same
bindings and output (see `DDGIVoxelTrace.slang`): negative distances for rays that start inside geometry, and
`SurfaceTolerance()` near zero. Scheduling, blending, relocation, classification, sampling and the editor need no
changes.

## Cost

Memory is listed in the inspector: about 52 MiB for Medium's three cascades (atlases, probe states, ray buffer), plus
the voxel scene
when VCT GI does not already allocate it. The [profiler](PROFILER.md) shows the GPU stages as **RHI DDGI / Probe
update** (reset, trace, relocate and classify, blend), **RHI DDGI / Apply** and **RHI Voxel Scene**.
`BasicRendererFrameStats` counts probes traced and reset, rays scheduled and frames per sweep.

See [the measurements](#measurements) below.

## Limitations

- Diffuse only; specular reflections still come from SSR and reflection captures.
- Detail is bounded by the voxel size (0.75 units for Medium's 48-unit, 64-voxel finest cascade). Thin walls can leak,
  and small emitters contribute through the voxels' coverage.
- Coverage is the widest cascade around the camera (about 400 units at the defaults); beyond it, surfaces keep the
  geometry pass's ambient light. Detail drops with each cascade's spacing, so distant bounce light is coarse.
- Changes reach a probe at its next update, so a light switched on takes a sweep (six to eight frames by default) plus
  hysteresis to settle.

## Measurements

`PlutoGEVulkanRhiTests --gi-performance`, 1920 x 1080, RTX 4070 SUPER. The scene is a static lit room with an
emissive panel; figures are the mean of 192 frames after 96 warm-up frames:

| Effect | Total | Trace | Relocate, classify and blend | Apply |
| --- | --- | --- | --- | --- |
| DDGI Low | 0.62-0.63 ms | 0.41 ms | 0.01 ms | 0.20 ms |
| DDGI Medium | 0.97-0.98 ms | 0.75-0.77 ms | 0.02 ms | 0.20 ms |
| DDGI High | 2.28-2.30 ms | 2.06-2.07 ms | 0.02 ms | 0.20 ms |
| SSGI defaults | 0.60-0.61 ms | 0.42-0.43 ms | 0.09 ms denoise | 0.07 ms composite |

Two runs, three cascades and multiple bounces. The GPU's clocks varied by about 1.5x between sessions, so compare the
rows with each other rather than with other measurements.

The voxel scene costs nothing measurable here because nothing in the room changes. Moving geometry or lights adds
VCT's progressive voxelization and relighting work (see [VCT world cache](vct-world-cache.md)). Tracing dominates, so
**Probes Per Frame** and **Rays Per Probe** are the main cost controls; Apply depends only on resolution.

## Validation

- `PlutoGEDdgiProbeGridTests`: storage addressing, scrolling, scheduling, settings sanitising, quality levels,
  packet round trips and the effect's parameter handling.
- `PlutoGEVulkanDdgiTests`, `PlutoGEOpenGLDdgiTests`: bounce light from an emitter, open-ground brightness (0.85 to
  1.05 of flat ambient), sky occlusion under a roof (below 0.9), and every debug view. It also checks identical
  bounce light with the grid straddling the world origin and wholly on its positive side, with partial updates. An
  integer `%` on negative cell indices once made surfaces left of or below the origin read other probes' data.
- `PlutoGEEngineGraphicsApiTests --ddgi-scene <project> <scene> <output> <frames> [eye target]` captures a project scene
  with DDGI off, on and in each debug view, for checking real content.
