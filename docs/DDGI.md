# Dynamic diffuse global illumination (DDGI)

**Dynamic Diffuse GI (Probes)** lights opaque surfaces with a grid of irradiance probes that follows the camera and
updates continuously from the scene. Unlike [ambient probe grids](ambient-probe-grids.md) nothing is baked: moving
lights, opening doors and changing emissive materials change the bounce light within a few frames. Unlike voxel cone
traced GI, the sky is occluded too, so interiors darken without manual volumes. The probes also occlude the sky in
reflections: where SSR finds no hit, glossy surfaces indoors reflect the probes' light instead of a sky they cannot
see. It works on Vulkan and OpenGL RHI; the legacy OpenGL renderer skips it.

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
- **Hysteresis** (default 0.97) is the share of a probe's history kept per update when the lighting really changed.
  Changes that double or halve a probe's brightness (a light switched on) use half of it, so large changes still
  settle in a few updates.
- **Stable Hysteresis** (default 0.98, never below Hysteresis) is the share kept while an update differs from the
  last by no more than its own ray noise. Each update traces a new random set of rays, so even a static scene's
  estimate changes a little every time; averaging those changes longer removes the shimmer without slowing real
  changes. Every texel measures its noise from the update's rays (the standard error of their weighted mean
  luminance): within one standard error the change counts as noise, beyond three as real, blending smoothly between.
  In Basin (Hysteresis 0.9) it halved the indirect light's flicker (0.068 to 0.030 at the pipe racks, 0.107 to 0.051
  in the hall), and an emitter switched off still faded in the same seven frames. Set it equal to Hysteresis to treat
  every change alike.
- **Normal Bias** and **View Bias** (in probe spacings) move lookups off the surface so probes behind it are not seen.
  The renderer never uses less than one and a half voxels for either, which is as sharply as the voxel scene places
  surfaces. A smaller view offset lit inside corners from probes outside the room (Medium's default view bias is only
  0.6 units against 0.75-unit voxels).
- **Leak Reduction** (default 0.3, 0 to turn off) stops light leaking into rooms from probes outside them. Each
  lookup weighs its eight surrounding probes by exact visibility from the [probe occlusion volume](#leak-reduction)
  and, when less than this share of their weight can see the surface, fades with that share instead of spreading the
  occluded probes' light over it. It matters most in rooms narrower than about two probe spacings, where every probe
  around the room can be outside it: a sealed 2.4-unit room at Medium received 49% of the outside light without it
  and 5% with it. Costs about 0.08 ms at Medium (1920 x 1080).
- **Probe Relocation** moves probes out of walls and away from nearby surfaces, by up to 0.45 spacings. A probe
  inside a surface thinner than one and a half voxels (a thin roof or wall) stays there, inactive: either side is as
  near, and probes moved out through it made the join of that surface and the ones beneath it flicker. A probe moves
  at most seven times, then stays where it is until it scrolls out of the volume or the volume resets: near some thin
  surfaces probes never settled and moved and snapped back at every update.
- **Relocate From Thin Surfaces** (off by default) moves those probes out as well. The voxel scene holds surfaces,
  not solids, so this covers any probe within about a voxel of a surface, such as a whole probe layer lying in a floor
  at a multiple of the probe spacing (shown as red probes in the Probes view). Off, surfaces near them use the next
  probes out, which in Basin's garage changed the floor's light by 1-2%; on, the joins of those surfaces with others
  can leak and flicker (in Basin's hall the beam under the roof did). Moving the floor off the probe layer avoids both.
- **Probe Classification** stops tracing all but 16 rays for probes inside geometry or with no surface within two
  spacings, since nothing samples them. This saves a lot in open areas.
- **Multiple Bounces** (on by default): rays that hit a surface also reflect the volume's own irradiance there. Over a
  few updates this bounces sky light, and every further bounce, through the scene. Without it, interiors only get
  direct and once-bounced light from the voxel scene and look much darker than they should. While it is on, the voxel
  scene's single **Secondary Bounce** is not gathered, since it would count that bounce twice.
- **Brightness Clamp** limits the luminance one ray gathers from the scene, so small, bright emitters do not flicker
  through single rays. Sky light is never clamped.
- **Reflection Occlusion** (default 1) is how much of the sky reflection the probes may remove where they see no sky
  along the reflection, replacing it with their own light from that direction. At 0, surfaces reflect the sky
  wherever SSR finds no hit, as without DDGI. Reflection captures are never occluded: only the sky's share of a
  surface's environment reflection changes. See [Reflection occlusion](#reflection-occlusion).
- **Voxel** settings (**Resolution**, **Cascades**, **Volume Size**, **Update Speed**, **Inject Local Lights**,
  **Local Light Bounce**, **Secondary Bounce**, **Voxelization Budget**) configure the voxel scene the rays march.
  They mean what they do for [voxel cone traced GI](vct-world-cache.md). When the camera also has **Voxel Cone
  Traced GI**, its voxel settings apply to both.
- **Debug View**: **Irradiance** shows the probe lighting alone. **Indirect Lighting** shows the diffuse light the
  surfaces receive. **Probes** draws each probe as a sphere shaded with its irradiance, depth-tested against the scene;
  inactive probes are dark red and probes waiting for their first update purple. **Probe Debug Radius** sizes the
  spheres. **Sky Visibility** shows how much sky the probes see along each pixel's reflection (white: open sky,
  black: fully occluded). **Reflections** shows the environment reflection after occlusion, which SSR falls back to.

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
   as the geometry pass lights surfaces. A ray that leaves above the horizon reports `PLUTO_DDGI_MISS_DISTANCE`; one
   that leaves below it reports the distance travelled, since it slipped through a floor thinner than a voxel or left
   past the cascades' lower faces, where the real world holds ground. Counted as sky, such rays made probes under
   Basin's hall floor (0.03 thick, nothing beneath) see sky downwards, and the undersides of objects and walls near
   them reflected it. With **Multiple Bounces**, a
   hit adds the voxel's albedo times the volume's irradiance at the hit point from the previous update. Each probe
   traces 16 fixed rays for relocation and classification, plus **Rays Per Probe** spherical-Fibonacci rays rotated
   randomly at every update.
4. **Probe update** (`DDGIProbeUpdate.slang`). Relocation and classification use the fixed rays. Rays are then blended
   into two octahedral atlases per probe: 6 x 6 texels of irradiance and 14 x 14 texels of hit distance, squared
   distance and sky visibility (the share of rays that missed), both with a one-texel border. Irradiance keeps
   **Stable Hysteresis** of its history for changes within the update's ray noise and **Hysteresis** beyond it;
   changes that double or halve a probe's luminance (above 0.1) converge at half the hysteresis. A probe's first
   updates after its history was replaced average evenly (the second keeps half, the third two thirds, up to fifteen
   updates), so its first noisy estimate does not linger at a high hysteresis. A probe that was inactive when traced
   has only its fixed rays, so it skips the blends even if classification reactivates it, and blends at its next
   update (blending the other ray slots once replaced its history with another probe's rays). Sky visibility uses
   a wider lobe than the distances (cosine to the 16th, half power at about 17 degrees): a distance texel's lobe holds
   about one ray per update, too few to estimate a share from.
   With **Leak Reduction**, the ray source then traces each listed probe's visibility into the probe occlusion volume
   (below), from the probe's position after relocation.
5. **Apply** (`DDGIApply.slang`). Each opaque lit pixel interpolates its eight surrounding probes, weighted by
   trilinear position, a smooth backface term, exact visibility from the occlusion volume and Chebyshev visibility,
   and fades where too few of them can see it (see [Leak reduction](#leak-reduction)). The result *replaces* the
   diffuse ambient the
   geometry pass wrote: sky, reflection-capture or baked-grid irradiance times the surface's diffuse response. It fades
   out over the grid's outermost cell. Unlit, glass and transparent surfaces keep their own ambient light.
   A pixel uses the finest cascade that contains it and blends into the next one out over that cascade's outermost
   cell; biases scale with each cascade's spacing. The same pass occludes the pixel's environment reflection (below)
   and writes the result where SSR reads its miss fallback.

### Leak reduction

Standard DDGI divides a lookup's weighted probe light by the sum of the weights, so the result does not depend on
how many probes see the surface. Where Chebyshev visibility rejects all of them, as it does for every probe around a
room narrower than the probe spacing (all outside it), that normalisation scales the rejected probes' tiny weights
back up to their full light: the room is lit from outside. Dropping the normalisation is no answer on its own,
because Chebyshev visibility, being statistical, also rejects every probe beside a wall at grazing angles; there
normalisation is what keeps the wall lit, and a lookup that trusted it went black.

So visibility comes from the scene itself. A **probe occlusion volume** divides every probe cell into 4 x 4 x 4
sub-cells (`PLUTO_DDGI_OCCLUSION_SUBDIVISION`), each holding four-bit visibility from its centre to each of the
cell's eight corner probes, stored like the probe data so scrolling moves nothing (`ddgiOcclusionIndex`). When a
probe updates, the ray source marches the segments from the sub-cells of its eight cells to it
(`IDdgiRaySource::TraceVisibility`; for voxels `segmentVisibility` in `DDGIVoxelTrace.slang`), so the volume follows
moving geometry and relocated probes at the probe update rate. A lookup interpolates its eight nearest sub-cells,
ignoring any that see no probe at all (they lie inside a surface's voxels and say nothing about the open space beside
them), multiplies each corner's weight by its visibility, and scales the result by the visible share of the corners'
unoccluded weight over **Leak Reduction**, at most one: normalised convolution with a certainty floor. Lookups that
see some probes are unchanged, and only where none or almost none do does the light fade.

The voxel march counts a layer only where its opacity exceeds one half, so a segment running beside a wall within
half a voxel of it is not blocked. It counts every layer it touches, including one an end lies in: voxels are coarser
than many rooms (a 0.3-thick wall fills a layer about 1.5 units deep at Medium), and nothing tells which side of the
wall such an end is on. That errs towards darkness at inside corners rather than towards leaks.

Measured with `PlutoGEVulkanRhiTests --ddgi-leakage` (sealed rooms with 0.3-thick walls under flat sky at Medium, five
placements against the grid): an 8-unit room received under 0.05% of the outside light in every placement, where
**Relocate From Thin Surfaces** had let in 4%; a 2.4-unit room 0-17%, 5% on average, against 35-65% without leak
reduction. In Basin it darkened interiors by 5-13%, about as much as the leaks had lit them.

### Reflection occlusion

Where SSR finds no hit (off screen, behind the camera, hidden), surfaces fall back to the environment reflection the
geometry pass wrote: the physical sky, or reflection captures where they cover the surface. Nothing checks whether
the sky is visible, so without DDGI glossy floors indoors reflect it. The probes already know: their rays that leave
the scene reach the sky.

The geometry pass writes the environment reflection with its sky share in alpha (one minus reflection-capture
coverage). `DDGIApply.slang` then, per pixel (`DDGIReflection.slang`):

1. Reads probe sky visibility and probe light along the reflection, with the same eight probes and weights as the
   diffuse lookup. The lookup direction leans towards the normal for rough surfaces (GGX's dominant direction) and
   stays at least about 24 degrees above the surface, since the probes see the surface itself just below its
   tangent plane and that surface cannot occlude its own reflection.
2. Replaces `occlusion = Reflection Occlusion x sky share x coverage x (1 - visibility)` of the reflection with the
   probes' light times the surface's split-sum specular response, adjusting the scene colour by the difference.
3. Writes the new reflection as the environment specular target that SSR, later in the frame, uses for misses, so
   SSR's own hits still replace exactly what the colour holds.

The geometry pass writes that target whenever DDGI is on (as for SSR). Sky visibility needs no extra atlas: it
uses the visibility atlas's spare channel.

Irradiance uses the renderer's existing ambient convention (cosine-weighted mean radiance times
`albedo (1 - F)(1 - metallic) / pi`). An unoccluded scene therefore looks as it does without DDGI: the GPU check
measures 0.96 of the flat-ambient brightness on open ground, the difference being ambient Fresnel, which the geometry
pass omits.

### Replacing the ray source

`DdgiVolume` depends only on `IDdgiRaySource`. A hardware ray-query source would implement `Trace` with the same
bindings and output (see `DDGIVoxelTrace.slang`): negative distances for rays that start inside geometry, and
`SurfaceTolerance()` near zero, and `PLUTO_DDGI_MISS_DISTANCE` (`DdgiConfig.h`) for rays that see the sky, which
reflection occlusion counts. Rays that leave the scene below the horizon should report a finite distance instead.
It also implements `TraceVisibility`, writing each listed probe's visibility from the sub-cells around it into the
probe occlusion volume (`DdgiVisibilityInputs` documents the layout); ray queries along the same segments would make
it exact. Scheduling, blending, relocation, classification, sampling and the editor need no changes.

## Cost

Memory is listed in the inspector: about 52 MiB for Medium's three cascades (atlases, probe states, ray buffer), the
probe occlusion volume (256 bytes per probe, about 5 MiB at Medium), plus
the voxel scene when VCT GI does not already allocate it. Screen-sized targets come on top: Apply's three outputs and,
when SSR does not already request it, the geometry pass's environment specular target (8 bytes per pixel each, about
16 MiB at 1920 x 1080). The [profiler](PROFILER.md) shows the GPU stages as **RHI DDGI / Probe
update** (reset, trace, relocate and classify, probe visibility, blend), **RHI DDGI / Apply** and **RHI Voxel
Scene**.
`BasicRendererFrameStats` counts probes traced and reset, rays scheduled and frames per sweep.

See [the measurements](#measurements) below.

## Limitations

- Reflections only lose sky the probes cannot see; their sharp detail still comes from SSR and reflection captures.
  What replaces the sky is the probes' irradiance along the reflection: low frequency, so a mirror floor indoors
  shows soft room colour where SSR misses rather than a sharp image, and that irradiance includes whatever sky its
  wide lobe sees. A radiance atlas of hits only, or ray-traced radiance, would sharpen and separate it
  (`DDGIReflection.slang` lists the approximations). Where captures and sky blend, occlusion scales the whole
  reflection rather than its sky part alone.
- With Reflection Occlusion on, interior reflections where SSR misses carry the probes' update noise at specular
  gain instead of the noise-free sky term. Under TAA it is not visible (the project stack below flickers less with
  occlusion on); without TAA it shows as grain of about half an 8-bit level.
- Detail is bounded by the voxel size (0.75 units for Medium's 48-unit, 64-voxel finest cascade). Walls thinner than
  a voxel fill one voxel layer, which filtered steps of a voxel would sample as half opaque at best; near the probe,
  where a ray's footprint is one voxel, the trace doubles opacity so such a layer stops it. Some sky light still
  reaches rooms along wall and roof junctions; a **Voxel Resolution** of 128 (High) halves the voxel and removes most
  of it. Small emitters contribute through the voxels' coverage.
- Rooms narrower than about two probe spacings have no probes of their own. Leak reduction keeps the outside light
  out of them, so they are lit only by what reaches probes outside through their openings; inside corners of rooms
  whose walls sit within a voxel of each other read slightly dark, as the occlusion volume cannot tell which side of
  a wall's voxels such a point is on. A finer **Probe Spacing** gives such rooms their own probes. Multiple bounces
  still carry a few percent of outside light into them (2.4-unit rooms received 0-17%, 5% on average).
- Leak reduction adds a little flicker where its visibility changes as probes update (Basin's indirect light: 0.031
  to 0.035 at the pipe racks).
- Coverage is the widest cascade around the camera (about 400 units at the defaults); beyond it, surfaces keep the
  geometry pass's ambient light. Detail drops with each cascade's spacing, so distant bounce light is coarse.
- Changes reach a probe at its next update, so a light switched on takes a sweep (six to eight frames by default) plus
  hysteresis to settle.

## Measurements

`PlutoGEVulkanRhiTests --gi-performance`, 1920 x 1080, RTX 4070 SUPER. The scene is a static lit room with an
emissive panel; figures are the mean of 192 frames after 96 warm-up frames:

| Effect | Total | Trace | Relocate, classify and blend | Apply |
| --- | --- | --- | --- | --- |
| DDGI Low | 0.71-0.80 ms | 0.40-0.47 ms | 0.02 ms | 0.30-0.31 ms |
| DDGI Medium | 1.06 ms | 0.73-0.74 ms | 0.02 ms | 0.30 ms |
| DDGI High | 2.33 ms | 1.99-2.00 ms | 0.03 ms | 0.31 ms |
| SSGI defaults | 0.61-0.63 ms | 0.43-0.44 ms | 0.09 ms denoise | 0.07 ms composite |

Two runs, three cascades, multiple bounces and reflection occlusion, before leak reduction. With it (0.3), a later
session measured Medium at 1.14 ms: 0.04 ms for probe visibility and 0.03 ms more Apply (0.33 ms). Reflection
occlusion costs about 0.1 ms of
Apply: the same session measured 0.19-0.20 ms without it, and no change to Trace. The GPU's clocks varied by about
1.5x between sessions, so compare the rows with each other rather than with other measurements.

In the Testing project's Sponza arcade (`--ddgi-scene ... preset=project://Managed/main.plutopostprocess full=1`, with
SSR, TAA and auto exposure), the mean second temporal difference of the final image was 0.32 with Reflection
Occlusion at 0 and 0.24 at 1: occlusion removes the sky sheen indoors without adding visible flicker.

The voxel scene costs nothing measurable here because nothing in the room changes. Moving geometry or lights adds
VCT's progressive voxelization and relighting work (see [VCT world cache](vct-world-cache.md)). Tracing dominates, so
**Probes Per Frame** and **Rays Per Probe** are the main cost controls; Apply depends only on resolution.

## Validation

- `PlutoGEDdgiProbeGridTests`: storage addressing, scrolling, scheduling, settings sanitising, quality levels,
  packet round trips, the effect's parameter handling, and that DDGI requests the environment reflection.
- `PlutoGEVulkanDdgiTests`, `PlutoGEOpenGLDdgiTests`: bounce light from an emitter, open-ground brightness (0.85 to
  1.05 of flat ambient), sky occlusion under a roof (below 0.9), and every debug view. Reflection occlusion under the
  physical sky: a glossy metal floor under a roof must lose its sky reflection (below 0.85 of the image without
  occlusion; 0.74 measured) and on open ground keep it (above 0.93; 0.99 measured). It also checks identical
  bounce light with the grid straddling the world origin and wholly on its positive side, with partial updates. An
  integer `%` on negative cell indices once made surfaces left of or below the origin read other probes' data.
  Switching the emitter off must fade its light as fast with Stable Hysteresis 0.99 as without it (seven frames).
  Probe reactivation: probes that traced a red floor once and went inactive must not light a wall that appears beside
  them red after the floor goes dark (blending their stale ray slots peaked at 0.09 of the light; now 0).
  Leakage: sealed rooms with 0.3-thick walls at five placements against the grid. An 8-unit room must stay below 0.5%
  of the outside light in every placement, a 2.4-unit room below 12% on average (5% measured), and the same narrow
  room must still leak over 30% with Leak Reduction off (49%), so the check keeps measuring leaks. `--ddgi-leakage`
  prints every placement.
- `PlutoGEEngineGraphicsApiTests --ddgi-scene <project> <scene> <output> <frames> [eye target]` captures a project scene
  with DDGI off, on and in each debug view, for checking real content.
