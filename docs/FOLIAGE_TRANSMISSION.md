# Foliage transmission

Leaves let light through. A closed canopy is not a roof: its undersides glow where the sun is behind them, and sky light
reaches the ground in reduced form through many layers. PlutoGE models this with one material property, **diffuse
transmission**, which the surface shader and both GI ray sources respect.

## Authoring

| Material property | Meaning |
|---|---|
| **Diffuse Transmission** (0–1) | The share of diffuse light that leaves the far side rather than the near side. Leaves are usually 0.2–0.45. |
| **Transmission Color** | The colour of that light. |
| **Follow Albedo** (0–1) | How far the colour follows a brightened albedo instead. 1 keeps veins and autumn colours when no colour is authored. |
| `DiffuseTransmissionTexture` | Optional map that multiplies the factor (alpha by default). |

Use them on two-sided surfaces that are thin. Above zero, a surface is also **foliage to GI**, so do not use this on walls:
GI rays pass through it.

Imports set them in two ways:

- `KHR_materials_diffuse_transmission` maps directly: factor, colour and texture.
- The **Vegetation LODs** import profile gives masked two-sided materials with no authored value a factor of 0.35 that
  follows albedo. Few sources author one: 6 of Verdant's 83 masked materials do.

Impostors use the same default, since their atlases hold no material maps.

**Existing projects must reimport vegetation models.** `PlutoGEImportModel --check` reports them as current after an
engine update, so force it, per model: `PlutoGEImportModel <project> <project://model.gltf> --force`.

## Surface shading

`BasicLit.slang`, on both the bound and the bindless path. Reflection loses what transmission takes: the albedo is
multiplied by `1 - factor`, and the far side's light is added with the transmitted colour.

- **Sun and local lights.** A surface facing away from a light lights up by `-N·L`. Only one of `N·L` and `-N·L` is above
  zero at a pixel, so each light takes a single shadow lookup. For the far side the lookup moves towards the light and its
  plane turns to the lit side, so a leaf does not find itself in the shadow map while other leaves still shade it. This also
  bypasses the directional shadow mask, whose planes face the camera. Contact shadows and micro-shadowing concern the near
  side and do not apply. Rectangle lights are a cosine emitter here, unshadowed.
- **Ambient light.** Sky, flat ambient, reflection captures and non-deferred baked volumes are evaluated again for `-N`.
- **Transparent pipelines** do not transmit: they would pay for lookups on an instruction budget OpenGL drivers already
  limit (NVIDIA's limit is 65535).

### What the G-buffer carries

Passes that replace ambient light after the geometry pass need the far side's response. The albedo target holds the reflected
albedo, and its alpha encodes the far side's response as a scale over it (`DiffuseResponse.slang`); 1 means none, which is
what decals and every other writer leave. A scale needs one channel, keeps per-texel colour and costs no new target, at
the price of taking its hue from the albedo rather than the exact transmitted colour; direct light uses the exact colour.

| Pass | What it does with it |
|---|---|
| `DDGIApply` | Interpolates the probes a second time for the far side, on translucent pixels only. |
| `DiffuseProbeResolve`, `SSGI`, `VCTTemporal` | Have one side's light only, which stands for both. |
| `SSAOUpsample` | Applies half the AO (the indirect target's alpha is 0.75): overlapping leaves are not closed creases. |

## Global illumination

Without this, a canopy was a solid roof to probe rays: one leaf fragment filled a voxel, and a hardware ray stopped at
every triangle. Probes also took leaves for walls, moved away from them, and lost sight of everything beyond.

The two sources share these rules:

1. Foliage dims a ray's radiance, partly and with depth.
2. Leaves do not place or switch off probes: relocation, classification and the occlusion volume see solid geometry only.
3. Reported hit distances, which drive probe visibility, are those of the next solid surface. A ray only foliage stopped
   reports a far distance that is not sky, so reflections under a canopy do not mirror the sky.
4. A hit on foliage gathers light from both of its sides.

### Voxels

`VCTVoxelize.slang` weights a leaf fragment by its alpha at the voxel's mip rather than testing one texel, so a voxel's weight
is an optical depth: leaf area over voxel face. `VCTVoxelOpacity.slang` turns that into opacity as `1 - exp(-0.5 x depth)`
(half the area of randomly turned leaves faces a ray), **capped below one half**.

The cap is what keeps walls and leaves apart. Rays read a sample of one half or more at the finest footprint as a solid
surface, because a wall thinner than a voxel filters to at least that; the march makes those samples opaque and keeps
everything below as it is. Surface records hold a solid bit (bit 30), so a voxel containing both counts as solid. Coarser
mips cannot tell the two apart and count as they read.

### Hardware rays

`RayTracingScene` builds foliage as non-opaque geometry under its own instance mask (`PLUTO_RT_MASK_FOLIAGE`).
`DDGIRayQueryTrace.slang` does not sample textures at hits, as elsewhere, so a ray meeting a leaf triangle stops by chance:
the material's mean alpha, less what transmission passes on, from 0.05 to 0.95. Each update's rays turn, so probe blending
averages the result. Rays that place probes and the occlusion volume trace with the solid mask and stay opaque. A radiance
ray that stops at a leaf traces once more for the solid distance. The cost is per foliage hit; solid scenes pay nothing.

Hardware rays still rebuild the top level whenever a traced draw changes, and foliage is usually most of the instances.
`RayTracingScene::Settings` (range and minimum radius) bounds them in dense scenes. In Verdant at 2560x1440 with a static camera
(2026-11-05), DDGI took 3.3 ms on hardware rays, of which the trace was 1.2 ms and acceleration structures under 0.01 ms.
A moving camera, which changes the traced set, has not been measured.

## Tests

`PlutoGE{Vulkan,OpenGL}RhiTests --foliage-transmission` (`tests/FoliageTransmissionChecks.h`):

- a backlit leaf is lit, takes the transmission colour and does not shadow itself;
- a caster between the sun and a leaf still blocks it;
- reflection loses the transmitted share, and energy stays bounded;
- a point light shines through;
- a canopy of foliage passes several times the sky light a solid one does, and still shades the ground, on both ray sources
  (hardware on Vulkan devices that have it).

`--ddgi-leakage` confirms walls still block light after the march's change. On Vulkan it stops at a known failure on hardware
rays, because they no longer leak enough for its check; use OpenGL for voxels.

## Judging it in a scene

`PlutoGERenderSceneShot` renders 16 frames by default, too few in a large scene: unresolved virtual shadow pages are lit, and
appear as hard rectangles on the ground with an overbright canopy, and DDGI has not converged. Pass `--frames 400`.
