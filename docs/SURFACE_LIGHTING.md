# Surface lighting

How opaque and transparent surfaces respond to light beyond the basic GGX model, and the screen-space terms that
refine it. Each feature lives in one place and is reached through a narrow seam, so it can be improved or replaced on
its own:

| Feature | Where | Control |
| --- | --- | --- |
| Microfacet BRDF for analytic lights | `shaders/SurfaceBrdf.slang` | always on |
| Multiple scattering (energy compensation) | `shaders/EnvironmentBRDF.slang` | always on |
| Geometric specular antialiasing | `SurfaceBrdf.slang` `surfaceAntialiasedRoughness` | always on |
| Spherical and tube lights | `SurfaceBrdf.slang` `brdfAreaLight` | Light > **Source Radius**, **Source Length** |
| Specular occlusion | `shaders/SSAOUpsample.slang`, `ScreenSpaceAmbientOcclusion` | SSAO > **Specular Occlusion** |
| Directional contact shadows | `shaders/ContactShadows.slang`, `ScreenSpaceContactShadows` | Directional light > **Contact Shadow Length** |
| Tone mapping curves | `shaders/ColorOperations.slang` `toneMapCurve` | Tone Mapping > **Curve** |

They work on the Vulkan and OpenGL RHI renderers; the legacy OpenGL renderer has none of them.

## Microfacet BRDF

`SurfaceBrdf.slang` is the BRDF every analytic light (the directional light, point and spot lights) is shaded with in
`BasicLit.slang`: Lambert diffuse, the GGX distribution, Schlick-GGX Smith masking with Karis's k = (r + 1)² / 8 remap
and Schlick Fresnel (exact dielectric Fresnel for glass). A surface is described once (`brdfSurface`) and each light
evaluated with `brdfEvaluate`, which returns diffuse and specular reflectance times n·l separately so glass and
shader-graph lighting can weigh them on their own. Shader-graph **custom lighting** replaces it entirely.

### Multiple scattering

Single-scattering microfacet models lose the light that bounces between microfacets: up to half of it on rough metals,
which then look dull and too dark next to smooth ones. Two corrections restore it:

- **Environment reflections** (`iblSpecularResponse`): Fdez-Agüera's multiple-scattering split sum. Its albedo is the
  split-sum fit's own, so a white metal reflects exactly the environment around it at every roughness (the white
  furnace test in `IblRenderingChecks.h`). Every pass that writes, replaces or occludes environment reflections
  (`BasicLit`, `DDGIApply`, both SSR paths) calls this one function, so they stay consistent.
- **Analytic lights** (`specularEnergyCompensation`): specular is scaled by 1 + F0 (1 / E − 1), with E the directional
  albedo of a white GGX metal. E comes from a cubic fit to numerically integrated values (within 0.04 for n·v ≥ 0.1),
  not from the split-sum fit, which underestimates albedo by up to 0.14 at low roughness and would brighten smooth
  metals.

Dielectrics barely change (F0 = 0.04). A white metal's direct specular rises by about 15% at roughness 0.5, 40% at
0.7 and 1.7-3.7x at roughness 1 (grazing to head-on); coloured metals by less. SSR's rough-reflection energy check
rose from 8,925 to 13,264.

### Geometric specular antialiasing

Highlights on dense or curved geometry alias into sparkle when the normal changes faster than a pixel. Roughness is
widened by the screen-space variance of the interpolated vertex normal (Tokuyoshi and Kaplanyan 2019, with Filament's
constants), so the specular lobe covers the normals the pixel spans. Normal maps are filtered by their mip chain
instead. The derivatives are taken in `surfaceDerivatives`, before any entry point can discard, so they stay defined
on every driver. The widened roughness is also what the G-buffer stores, so SSR and specular occlusion agree with it.

### Spherical and tube lights

Point and spot lights take an emitter shape: **Source Radius** (a sphere, metres) and, for point lights, **Source
Length** (a tube along the light's forward axis). Ideal points give pinpoint highlights that read as small and harsh;
a source of a few centimetres gives the soft, sized highlights of real bulbs and strip lights.

Specular uses the representative point on the light nearest the reflection ray, with the distribution's
normalisation widened by the light's angular size so energy stays about the same (Karis, "Real Shading in Unreal
Engine 4", 2013). Diffuse, falloff and shadows keep the light's centre: at the distances lights are seen from, a
sphere's diffuse differs from a point's by little, and shadows would need a different filter per light size.
Rectangle lights (linearly transformed cosines) are not implemented: they need lookup tables and a different light
record, for a shape most scenes approximate with tubes.

The shape travels as `BasicPointLight::sourceRadius`, `sourceLength` and `sourceAxis`, packed per light into
`pointShape` (`SceneLightingParameters.slang`); spot lights ignore length.

## Specular occlusion

Ambient occlusion used to darken only diffuse ambient light, so reflections stayed full strength in creases, under
objects and inside cavities, where a glossy surface would see mostly its surroundings. SSAO's composite now also
occludes the environment reflection by the share of its GGX lobe the AO cone leaves visible (Lagarde and de
Rousiers, "Moving Frostbite to PBR", 2014): rough lobes follow AO, smooth lobes are occluded only where AO is strong
and the view grazing.

**Specular Occlusion** (default 1, 0 to 1) scales it. The composite writes the occluded reflection as the frame's
environment reflection, so SSR, which runs later, falls back to it and replaces only what it finds on screen; DDGI's
reflection occlusion (sky visibility) runs earlier and composes with it.

It needs material roughness and the environment reflection from the geometry pass, which SSAO now requests
(`GeometryInputsFor`). With Specular Occlusion at 0, SSAO keeps the narrower indirect-only layout it used before.

## Contact shadows

Shadow maps cannot resolve occluders within a few texels of their receivers, so feet, props and creases look
detached and small objects cast nothing. After the depth prepass, `ScreenSpaceContactShadows` marches a short ray
from every pixel towards the directional light through the coverage depth (12 steps, dithered per pixel and frame for
TAA to average) and writes a mask of the light's visibility with the device depth it was traced for. Surfaces
multiply their directional shadow by the mask only where their own depth matches, so a draw the prepass did not
include (blended, outlines, graphs that read scene textures) keeps its own shadowing rather than one traced for
whatever lay behind it.

**Contact Shadow Length** (directional light, default 0.25 m, 0 to 2) is how far the rays reach. Longer rays catch
larger occluders but show screen-space limits more: occluders off screen or behind other surfaces cast nothing, and
an occluder is assumed to be half the ray length thick behind its depth.

Contact shadows need the depth prepass (on by default) and directional shadows enabled; without either they are off.
Spot and point lights have none yet: the mask holds one light.

## Tone mapping curves

**Curve** on Tone Mapping selects how scene radiance maps to the display:

- **ACES** (default, Narkowicz's fit): contrasty and saturated. Per-channel, so bright saturated colours skew in hue
  as they clip: fire turns yellow, blue lights turn cyan, skin highlights turn orange.
- **AgX** (Wrensch's fit of the default look): hue-preserving; bright colours bleach towards white as on film. The
  look most current engines and Blender default to; recommended for new scenes.
- **PBR Neutral** (Khronos): reproduces base colours exactly up to 0.76 and compresses only highlights, so materials
  look as authored in other viewers. Best for product and asset viewing.

ACES stays the default so existing scenes keep their look. All three return linear values, which **Gamma** (or Gamma
Correction) encodes; the fused colour pass and the standalone pass share `toneMapCurve`.

## Cost

Measured in Basin (1552 x 814, RTX 4070 SUPER, Vulkan): contact shadows 0.06 ms (**RHI Contact shadows**); the SSAO
composite with specular occlusion 0.05 ms of SSAO's 0.13 ms. The BRDF changes add a few ALU instructions per light.
With Specular Occlusion on, SSAO-only stacks also write the environment reflection target in the geometry pass (one
RGBA16F target).

On OpenGL the lit and glass shaders take about 10 s each to compile when the driver has not cached them (after any
change to `BasicLit.slang` or its includes); the driver caches them once a process exits normally.

## Validation

- `PlutoGEVulkanRhiTests --ibl-only` (and OpenGL): white furnace at roughness 0.3, 0.6 and 1 within 6%.
- `--ssao-only`: without specular occlusion a metal's reflection is unchanged; with it, the crease darkens and
  nothing brightens.
- `--contact-shadows` (also in the default run): a cube that casts no shadow-map shadow darkens the floor beside it;
  a bare floor is unchanged (no self-shadowing).
- `--area-lights` (also in the default run): a sphere widens a highlight, a tube stretches it along its axis.
- `--render-optimizations`: each tone mapping curve renders alike fused and standalone, and each differs. (On Vulkan
  this check is preceded by a stationary-parameter upload check that fails at baseline.)
- `PlutoGERhiPostProcessAdapterTests`: the curve and specular occlusion lanes, defaults, clamping and SSAO's geometry
  inputs.

## Not implemented

- Material ambient occlusion maps and micro-shadowing (materials have no occlusion texture yet).
- Clear coat and sheen layers.
- Rectangle area lights (LTC), and contact shadows for local lights.
- Hardware ray tracing: `IDdgiRaySource` is the seam for DDGI; reflections and shadows would need their own.
