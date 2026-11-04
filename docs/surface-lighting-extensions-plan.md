# Surface lighting extensions: implementation plan

Status (2026-11-02): all four phases implemented; [SURFACE_LIGHTING.md](SURFACE_LIGHTING.md) and [DDGI.md](DDGI.md)
document the result. Hardware ray tracing is compiled and fallback-tested on the development machine (Vega 8, no ray
queries); its traced path (`--ray-tracing`, DDGI with Ray Source Hardware) still needs a run on RT hardware.

Implements the four items under "Not implemented" in [SURFACE_LIGHTING.md](SURFACE_LIGHTING.md):

1. Material ambient occlusion maps and micro-shadowing.
2. Clear coat and sheen layers.
3. Rectangle area lights (LTC), and contact shadows for point and spot lights.
4. Hardware ray tracing, starting with a ray-query DDGI ray source.

The renderer is forward+: `BasicLit.slang` lights every surface in the geometry pass and writes G-buffer-style
targets that the post passes (SSAO, DDGI, SSGI, SSR, probe resolve) read. Every phase targets the Vulkan and OpenGL
RHI renderers. (The legacy OpenGL renderer, untouched by this plan, has since been removed.)

## Design principles

- **One table per extensible list.** Material texture slots become a registry (`MaterialTextureSlots.h`) that drives
  serialization, reference scanning, migration, the editor and import references, so a future slot is added in one
  table plus its GPU binding instead of in about thirty places.
- **Lobes and light shapes live in their own includes.** `SurfaceBrdf.slang` stays the base microfacet model. Coat
  and sheen go in `SurfaceLayers.slang`; LTC rectangle evaluation in `LtcAreaLights.slang`. `BasicLit` composes them
  and stays the only place that knows about all of them.
- **Screen-space passes consume, never guess.** Anything a post pass needs (material AO) is written to the G-buffer
  explicitly, so DDGI, SSGI and the probe resolve apply it when they replace indirect light.
- **Capabilities, not API checks.** Hardware ray tracing is a capability query on the RHI device. Renderers select
  implementations through interfaces (`IDdgiRaySource`) with a working fallback, so OpenGL and non-RT GPUs lose
  nothing.
- **Backward compatible data.** New material keys and light properties default to "off", so existing assets render
  identically; enum values are only ever appended.

## Phase 1: material texture slot registry and material AO

### Slot registry

- `engine/render/include/PlutoGE/render/MaterialTextureSlots.h`: `enum class MaterialTextureSlot`, a constexpr table
  of `MaterialTextureSlotInfo { slot, assetKey, channelKey, displayName, colorSpace, defaultChannel }`, and
  `Texture *MaterialTexture(const MaterialConfig &, slot)` / `SetMaterialTexture` / `MaterialTextureChannel`.
- `MaterialTextureReferences` becomes an array indexed by slot (with `operator[]`).
- Table-driven: `WriteMaterialAsset`, `AssetManager::LoadMaterialAsset`/`SaveMaterialAsset` texture keys,
  `AssetReferences.cpp`, `AssetMigrationSerialization.cpp`, the material editor's slot list, `ModelImportService`.
- Existing slots keep their asset keys (`AlbedoTexture`, ...), so files are unchanged.

### Material AO

- Slot `Occlusion` (linear, channel select, default red), scalar `occlusionStrength` (0 to 1, default 1).
- **Shading** (`BasicLit.slang`, new `MaterialOcclusion.slang`):
  - Diffuse ambient is multiplied by AO, with Jimenez's multi-bounce fit (GTAO, 2016), so bright albedos darken
    less.
  - Environment specular is occluded with `ssaoSpecularOcclusion` (Lagarde), the same model SSAO uses.
  - Micro-shadowing for direct light (Chan, "Material Advances in Call of Duty: WWII", 2018):
    `visibility = saturate(abs(n.l) + 2 ao^2 - 1)`, applied to every analytic light.
- **G-buffer**: material AO goes in `material.a` (today a copy of alpha that nothing reads). `DDGIApply`, `SSGI` and
  `DiffuseProbeResolve` multiply the indirect diffuse they write by it, so the occlusion survives replacement. SSAO
  already scales the indirect target, so the two combine multiplicatively.
- **Import**: glTF `occlusionTexture` (+ strength), including the ORM case where it shares the metallic-roughness
  image; Assimp `aiTextureType_AMBIENT_OCCLUSION`.
- **GPU**: `BasicDraw` handle + channel; `BasicMaterialParameters` lanes; bound binding 26; bindless record indices.
- **Tests**: serialization round-trip, import of an ORM glTF, a rendering check (an AO-mapped quad is darker in
  ambient light only where AO is low, unchanged when strength is 0, and direct light is micro-shadowed).

## Phase 2: clear coat and sheen

- **Material**: `clearCoat`, `clearCoatRoughness`, `sheenColor`, `sheenRoughness`; slots `ClearCoat`,
  `ClearCoatRoughness`, `SheenColor`, `SheenRoughness` (glTF's channel conventions: coat R, coat roughness G,
  sheen colour RGB, sheen roughness A).
- **Shading** (`SurfaceLayers.slang`), layered over the base BRDF in `BasicLit`:
  - Clear coat: an isotropic GGX lobe with F0 0.04 and Kelemen visibility (Filament). The base layer is attenuated by
    `1 - Fc`. Environment: a second reflection sample at coat roughness through the same capture/sky path, with
    `iblSpecularResponse(0.04, ...)`.
  - Sheen: Charlie distribution (Estevez and Kulla 2017) with Neubelt visibility; the base is scaled by
    `1 - max(sheenColor) * E_sheen`, E from an analytic fit. Environment sheen uses the same albedo fit with the
    diffuse irradiance as the incoming light (a cheap, energy-plausible approximation).
  - Both are skipped by uniform branches when their factor is zero, so existing materials pay nothing measurable.
- **Interaction with post passes**: the coat reflection is added to the environment specular target, so SSR replaces
  it where it hits (base and coat share one screen trace). Documented as a limitation.
- **Import**: `KHR_materials_clearcoat`, `KHR_materials_sheen`.
- **Not in scope**: clear-coat normal maps and shader-graph outputs for the layers (the graph's output register
  lanes are full; adding them is a separate change to `ShaderGraphProgramData`).
- **Tests**: round-trip, import, rendering checks (coat adds a sharp highlight over a rough base; sheen brightens
  grazing edges; zero factors render identically to before).

## Phase 3: rectangle lights and local contact shadows

### Rectangle lights

- `LightType::Rect` (appended), `sourceWidth`, `sourceHeight`; `Light` stores the full orientation (right and up
  axes) instead of only the forward direction. One-sided, emitting along forward.
- **GPU record**: no cbuffer growth. A rect reuses `pointDirectionSpot` (forward, type 2 in w) and `pointShape`
  (right axis times half-width, half-height in x; up = cross(forward, right)). A small `LocalLightShape` decoder in
  `SceneLightingParameters.slang` hides the packing from every consumer.
- **Shading** (`LtcAreaLights.slang`): Heitz et al. 2016. Diffuse is the exact clipped-polygon form factor;
  specular is the polygon integrated against the LTC fitted to GGX, with Fresnel from the second LUT. The LUTs
  (64 x 64, RGBA16F x 2) come from `tools/ltc_fit`, an offline fitting program whose generated header is committed
  (`engine/render/src/LtcTables.inl`), uploaded once by the renderer and bound next to the BasicLit set.
- Clustering keeps the sphere bound of the light's range. Shadow maps are not added for rect lights (contact
  shadows apply). VCT relighting and particles treat a rect as a cosine-weighted point at its centre.
- Editor: inspector width/height, viewport gizmo (outline and normal), hierarchy preset.

### Contact shadows for local lights

- `ScreenSpaceContactShadows` generalises from one direction to up to four **contact lights**: the directional light
  (when active) and the local lights with the largest screen contribution that have a non-zero **Contact Shadow
  Length**. Each entry holds a view-space direction (directional) or position (local) and a length.
- The mask becomes RGBA8 visibility for four slots plus the R32F depth it was traced for (two targets of one pass).
- Each light records its slot (or none) in a new `localLightContactSlot` cbuffer lane; `BasicLit` multiplies the
  light's visibility by its slot's mask channel. Local rays stop at the light.
- `Light` gains **Contact Shadow Length** (default 0 for local lights, so existing scenes are unchanged).
- **Tests**: the existing directional check still passes; a point light beside a cube darkens the floor at the cube's
  foot only with a length set; a rect light shades a floor like a dense grid of points (diffuse within a few percent)
  and its highlight follows the rectangle's shape.

## Phase 4: hardware ray tracing

### RHI

- `RayTracingSupport IRenderDevice::GetRayTracingSupport()` (`{supported, reason}`, default unsupported).
- Types: `AccelerationStructureHandle`; `BufferUsage::AccelerationStructureInput`;
  `ResourceBindingType::AccelerationStructure`; `BarrierAccess::AccelerationStructureBuild/Read`.
- `ICommandContext::BuildBottomLevel(...)`, `BuildTopLevel(...)`, `BindAccelerationStructure(slot, handle)`;
  forwarded by `DeferredCommandList`. OpenGL keeps the throwing defaults and reports unsupported.
- **Vulkan**: when the device exposes `VK_KHR_acceleration_structure` + `VK_KHR_ray_query` (+ deferred host
  operations), enable them, `bufferDeviceAddress`, and VMA's device-address flag; add AS resources, scratch
  allocation, build commands, descriptor pool sizes, layout and write support. Opt out with
  `PLUTO_VULKAN_RAY_TRACING=0`.
- **Shaders**: a `pluto_add_slang_ray_query_shader` CMake function compiles SPIR-V 1.4 with ray query only (no GLSL).

### Ray-traced scene and DDGI source

- `engine/render/src/raytracing/RayTracingScene`: one BLAS per (mesh, submesh, revision), built on demand and
  compacted; a TLAS rebuilt each frame from the GI draw list (static and moving opaque geometry; skinned meshes stay
  excluded as they are from GI today); an instance record buffer (albedo, emission, base colour texture index when
  bindless is available).
- `RayQueryDdgiRaySource : IDdgiRaySource` (`DDGIRayQueryTrace.slang`): triangle hits shaded with the hit's albedo
  times sun (with a shadow ray), local lights (unshadowed) and the volume's own irradiance for multiple bounces;
  misses sample the sky exactly as the voxel source does. `TraceVisibility` uses exact segment queries.
  `SurfaceTolerance()` is a few millimetres.
- `BasicRenderer` holds an `IDdgiRaySource` chosen by a new DDGI **Ray Source** setting (Auto, Voxels, Hardware).
  Auto uses hardware when the device supports it and falls back otherwise.
- **Tests**: capability reporting on both APIs; `RayTracingScene` bookkeeping (BLAS reuse by revision); on GPUs with
  ray queries, the existing DDGI checks rerun with the hardware source; elsewhere they are skipped with a message.
  The development machine (Vega 8) has no ray queries, so the hardware path is compiled and fallback-tested here and
  needs validation on RT hardware.

## Order and verification

Phases land in order; each builds with `msvc18-release` and runs the Vulkan and OpenGL RHI checks it touches plus
`PlutoGEMaterialReloadTests`, `PlutoGEModelImport*`, `PlutoGERhiPostProcessAdapterTests`. Suites that already fail
at HEAD (VSM-only, render-optimization, VCT world cache, preparation cache, GL shader-graph) are not regressions.
`SURFACE_LIGHTING.md` is updated as each feature lands.
