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
| Material ambient occlusion and micro-shadowing | `shaders/AmbientOcclusion.slang` | Material > **Occlusion Texture**, **Occlusion Strength** |
| Clear coat and sheen layers | `shaders/SurfaceLayers.slang` | Material > **Clear Coat**, **Sheen** |
| Specular occlusion | `shaders/SSAOUpsample.slang`, `ScreenSpaceAmbientOcclusion` | SSAO > **Specular Occlusion** |
| Rectangle lights | `shaders/LtcAreaLights.slang` | Light > **Light Type** Rect, **Source Width**, **Source Height** |
| Contact shadows | `shaders/ContactShadows.slang`, `ScreenSpaceContactShadows` | Light > **Contact Shadow Length** |
| Tone mapping curves | `shaders/ColorOperations.slang` `toneMapCurve` | Tone Mapping > **Curve** |

They work on both the Vulkan and OpenGL renderers.

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
Rectangles are their own light type (below).

The shape travels as `BasicPointLight::sourceRadius`, `sourceLength` and `sourceAxis`, packed per light into
`pointShape` (`SceneLightingParameters.slang`); spot lights ignore length.

## Rectangle lights

**Rect** lights are one-sided rectangles of **Source Width** x **Source Height** metres, facing the entity's forward
axis with their width along its x axis (the hierarchy's *Rect Light* preset hangs one from the ceiling). **Intensity**
is candela along the normal, as for the other local lights, so far away a rectangle lights like a point with a cosine
falloff; up close its light, and its reflection in glossy surfaces, take its shape.

They are shaded with linearly transformed cosines (Heitz, Dupuy, Hill and Neubelt, 2016): a cosine lobe transformed
by a 3 x 3 matrix approximates GGX at each roughness and view angle, and its integral over the rectangle (clipped to
the surface's horizon) has a closed form. Diffuse is the exact form factor. `tools/FitLtcTables.py` fits the matrices
and the BRDF's albedo terms (64 x 64; checked against Monte Carlo integration: specular within 1-8% at head-on and
moderate views, up to about 30% at grazing ones, LTC's known weak spot) into `LtcTables.inl`, which the renderer
uploads as one RGBA16F texture (set 0 binding 8). Clear coat uses a second lookup at its roughness; sheen reflects
the form factor times its albedo.

Rectangles pack after points and spots in the 32 local-light slots, reusing their lanes (`pointDirectionSpot.w` = 2,
`pointShape` = half-height and right axis times half-width; `localLightType` decodes them). Limits: no shadow maps
(contact shadows apply); shader-graph custom lighting and voxel GI see a point at the centre with a hemispherical
cosine; particles and the probe baker treat them as point lights.

## Material ambient occlusion

Materials take an **Occlusion Texture** (linear, with a channel selector; glTF's `occlusionTexture`, often the red
channel of a packed occlusion-roughness-metallic map) and an **Occlusion Strength** (0 to 1, default 1; 0 ignores the
map). It darkens three things, all with the formulas in `AmbientOcclusion.slang` that SSAO shares:

- **Diffuse ambient light**, with Jimenez's multi-bounce fit ("Practical Real-Time Strategies for Accurate Indirect
  Occlusion", 2016): light bouncing inside a cavity lifts bright albedos, so a white crevice darkens less than a black
  one. Never darker than the occlusion itself.
- **Environment reflections**, with the same specular occlusion SSAO uses (`specularOcclusion`).
- **Direct light (micro-shadowing)**, from every analytic light: a cavity is treated as a cone of aperture 2 ao^2
  around the normal, so light from inside the cone is unshadowed and light from outside it fades (Chan, "Material
  Advances in Call of Duty: WWII", 2018). Fully occluded texels receive no direct light.

Passes that rebuild ambient light after the geometry pass would otherwise lose the occlusion, so the geometry pass
writes it to the material target's alpha (previously an unread copy of opacity) and `ambientDiffuseResponse`
(`DiffuseResponse.slang`) applies it; DDGI, SSGI and the deferred probe-grid resolve all use that function. SSAO
scales the indirect target, which already holds it, so material and screen-space occlusion multiply.

## Clear coat and sheen

Two optional layers over the base BRDF, each off at its default:

- **Clear Coat** (strength 0 to 1) and its **Roughness**: a thin, smooth dielectric over the surface (lacquer, car
  paint, varnish). An isotropic GGX lobe with F0 0.04 and Kelemen's visibility, as Filament and glTF's
  `KHR_materials_clearcoat`. The base receives and returns the light the coat transmits (1 - its Fresnel), and the
  coat reflects the environment a second time at its own roughness. Textures scale both factors (glTF: red and green).
- **Sheen** colour and **Roughness**: back-scattering fibres standing off the surface (cloth, velvet, dust). The
  Charlie distribution (Estevez and Kulla, 2017) with Neubelt's visibility, as `KHR_materials_sheen`. The layers below
  are scaled by 1 - max(sheen colour) E, E the lobe's directional albedo; environment sheen is E times the diffuse
  irradiance. E is a 16 x 16 table in the shader (integrated numerically; bilinear lookup within 0.04), so it needs no
  texture binding. A sheen colour texture (sRGB) and roughness texture (glTF: alpha) scale the factors.

Layers are evaluated per light in `layersEvaluate` and for the environment through `layersEnvironmentBaseScale`,
`coatEnvironmentResponse` and `sheenEnvironmentResponse`; materials whose factors are zero skip them by uniform branch
and render exactly as before. Glass ignores both.

Limits: the coat shares the base's normal (no clear-coat normal map), and shader-graph materials have no layer
outputs (the graph's output register lanes are full). SSR traces the base lobe only, so where it hits it replaces
the coat's environment reflection with the base's; DDGI and SSGI rebuild ambient light without sheen.

## Material texture slots

`MaterialTextureSlots.h` lists every texture a material can reference with its asset key, channel key, colour
space and `MaterialConfig` members. Serialization, dependency scanning (`MaterialTextureSlotKeys.h`, free of rendering
dependencies), migration, inline mesh-component materials, the material editor and model import iterate it, so a
new slot is a table entry, its `MaterialConfig` fields and its GPU binding. On the GPU, maps beyond the core set are
`BasicSurfaceTexture`s: bound as a group from `BasicRenderer::SurfaceTextureSlot` (set 1 bindings from 26), with
presence bits and channels in the material parameters (`BasicSurfaceParameters.h`, shared by the bound and bindless
paths).

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
from every pixel towards each of up to three lights through the coverage depth (12 steps each, dithered per pixel
and frame for TAA to average) and writes a mask of their visibilities (RGB) with the device depth they were traced
for (A). Surfaces multiply a light's visibility by its channel only where their own depth matches, so a draw the
prepass did not include (blended, outlines, graphs that read scene textures) keeps its own shadowing rather than one
traced for whatever lay behind it.

**Contact Shadow Length** (every light type; 0 to 2 m) is how far the rays reach: 0.25 m by default for the
directional light, 0 (off) for point, spot and rectangle lights. Longer rays catch larger occluders but show
screen-space limits more: occluders off screen or behind other surfaces cast nothing, and an occluder is assumed to
be half the ray length thick behind its depth. Rays towards a local light stop at it.

The directional light takes a channel first; local lights with a length that reach the camera take the rest,
brightest there (intensity over squared distance) first, so at most three lights have contact shadows in a frame.
`contactShadowParameters.xyz` records which light each channel holds. Contact shadows need the depth prepass (on by
default) and shadows enabled. They are the only shadows rectangle lights cast.

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
- `--contact-shadows` (also in the default run): a cube that casts no shadow-map shadow darkens the floor beside it,
  for the directional light, a point light and a rectangle light (local lights only with a length set); a bare floor
  is unchanged (no self-shadowing), also at a grazing angle.
- `--rect-lights` (also in the default run): a small rectangle lights a floor as a point light of its intensity, lights
  nothing facing away, and its glossy reflection follows its orientation.
- `--area-lights` (also in the default run): a sphere widens a highlight, a tube stretches it along its axis.
- `--material-occlusion` (also in the default run): occlusion 0 removes ambient light, strength 0 and the channel
  selector are honoured, half occlusion keeps some head-on light and removes light from 60 degrees.
- `--surface-layers` (also in the default run): a smooth coat adds a head-on highlight over a rough base and only
  takes light it reflects away; sheen brightens a grazing view; zero factors change nothing.
- `PlutoGEMaterialReloadTests`: glTF import of packed occlusion, clear coat and sheen (and the importer's cache),
  their slots and channels, and save/reload.
- `--render-optimizations`: each tone mapping curve renders alike fused and standalone, and each differs. (On Vulkan
  this check is preceded by a stationary-parameter upload check that fails at baseline.)
- `PlutoGERhiPostProcessAdapterTests`: the curve and specular occlusion lanes, defaults, clamping and SSAO's geometry
  inputs.

## Hardware ray tracing

The RHI exposes inline ray queries where the GPU has them (`IRenderDevice::GetRayTracingSupport`, acceleration
structures, `ResourceBindingType::AccelerationStructure`; Vulkan only). DDGI uses them through its hardware ray
source ([DDGI](DDGI.md#hardware-ray-tracing)), and `RayTracingScene` keeps the traced scene. Reflections and shadows
do not trace rays yet: a ray-traced reflection or shadow pass would bind the same top-level structure.

`--ray-tracing` (also in the default run) reports support; with it, rays from above must hit a quad's two instances
at their distances with their custom indices and normals and miss between them; without it, acceleration structures
must be refused.
