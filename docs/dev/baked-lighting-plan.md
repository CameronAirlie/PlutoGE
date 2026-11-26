# Baked lighting plan: lightmaps for shadows and GI, static/dynamic choice

Status: phases 1-5 implemented (2026-11-26); validation on RocketLeg done headlessly (see Validation). User-facing reference: [BAKED_LIGHTING.md](../BAKED_LIGHTING.md).

## Goal

Static geometry gets baked shadows and global illumination from lightmaps; the scene chooses whether that baked
lighting is used at all (**Realtime** or **Baked** lighting); each light chooses how it takes part (**Realtime**,
**Mixed** or **Baked**). Dynamic objects (characters, physics props) keep realtime shadows from Mixed lights and receive
baked light through the existing ambient light volumes.

## History

Lightmaps existed until `392f4d18` (2026-11-04), which removed them because they were OpenGL-only textures no RHI
shader sampled. That baker kept one PFM per submesh, cloned a material per submesh and generated UV2 as one grid cell
per triangle. This plan keeps what was sound (texel rasteriser, multi-bounce gather, edge-aware denoise, dilation) and
replaces the parts that were not: lightmaps become one RHI texture sampled by every lit path, ownership moves to one
scene component, and UV2 generation moves into the asset import pipeline.

## Light modes

| Mode | Static (lightmapped) receivers | Dynamic receivers | Shadows from dynamic casters |
| --- | --- | --- | --- |
| Realtime | Realtime direct + realtime shadows; no baked contribution | Realtime | Yes |
| Mixed | Realtime direct; shadow = min(realtime shadow, baked shadow mask); indirect baked | Realtime direct + shadows; indirect from probes | Yes |
| Baked | Direct and indirect from the lightmap; no realtime cost | Direct and indirect from the probe volume | No |

- Mixed is the recommended mode for the sun (new lights default to Realtime): the sun stays realtime (specular, normal
  maps, character shadows) and static geometry gains baked shadows beyond the realtime shadow distance, even with
  realtime shadows off.
- The baked shadow mask is one channel (the lightmap's alpha), owned by the first Mixed directional light. Mixed local
  lights bake their bounce but keep realtime shadows only ("baked indirect"). More mask channels are an extension.
- Modes only change rendering while the scene's lighting is **Baked** and a bake is loaded; otherwise every light renders
  realtime, so an unbaked scene is never dark.
- Migration: the old light `Static` flag maps to Mixed (it meant "bake my bounce, keep me realtime"); non-static lights
  are Realtime.

## Architecture

```
Editor (Lighting settings, light Mode, Bake)             docs: BAKED_LIGHTING.md
        |
scene::SceneBaker  -- orchestrates stages --> ProbeVolumeStage (existing)
        |                                 \-> LightmapStage (new)
        v                                          |
scene/bake/BakeRayScene  (shared CPU ray kernel: triangles, BVH, lights, albedo, trace, direct light)
scene/lightmap/LightmapPacker (chart rects -> atlas)   scene/lightmap/LightmapRasterizer (UV2 -> texels)
scene/lightmap/LightmapIntegrator (direct, shadow mask, multi-bounce GI)   LightmapFilters (dilate, denoise)
scene/lightmap/LightmapFile (.plutolightmap read/write)
        |
BakedLightingComponent (scene settings + bake output: atlas file, per-draw rects) -- Scene::GetBakedLighting()
        |
MeshComponent::SubmitRenderCommands -> RenderCommand::lightmap -> BasicDraw::lightmap
RhiSceneRenderer -> BasicLighting::bakedLighting (atlas texture, directional mode) ; filters Baked local lights
BasicRenderer -> object params / bindless records / frame params -> BasicLit.slang (LightmapSampling.slang)
assets: MeshImporter "Generate Lightmap UVs" -> LightmapUvUnwrapper (planar charts) -> cooked UV2
```

Seams that keep features modular:

- **One ray kernel.** The probe and lightmap bakes share `BakeRayScene`; a GPU backend can replace it later without
  touching stages.
- **Stages.** The probe volume and lightmaps are independent stages over one prepared scene; new outputs (terrain
  lightmaps, baked AO, light-probe groups) are new stages.
- **Data owned once.** `BakedLightingComponent` owns settings and output; meshes look their rect up when they rebuild
  commands, and a rebake dirties them. No per-submesh material clones.
- **Encoding is explicit.** `LightmapEncoding` in the file header (v1: irradiance RGB + directional shadow mask A, half
  float). Directional lightmaps or more mask channels are new encodings, not format breaks.
- **UV2 at import.** The unwrapper sits behind `GenerateLightmapUvs(MeshData&, settings)`; xatlas could replace the
  built-in planar chart unwrapper without changing callers.

## GPU contract

- Per draw: `float4 lightmapScaleOffset` (UV2 * xy + zw; zero scale = not lightmapped) in `BasicObjectParameters`
  (also the object stream / instanced block) and `BindlessInstanceRecord`. Lightmapped draws never merge into instanced
  batches (the rect is part of `SameBasicDrawShading`); storage-instanced draws are never lightmapped.
- Per frame: `bakedLighting` (x lightmaps on, y directional mode, zw atlas size) in `BasicFrameParameters`; the atlas
  at logical slot 42, set 0 binding 42 (OpenGL unit 42: the GL backend binds textures to the unit named by the slot).
- Stored value: cosine-weighted mean radiance (irradiance / pi), the convention every ambient layer in
  `BasicLit.slang` multiplies by `diffuseResponse`.
- Indirect target alpha `BakedIndirectAlpha` (0.375): DDGI and SSGI keep the baked light (they replace only >= 0.5),
  SSAO still applies.

## Phases

1. **Data and modes**: `LightBakeMode` on lights with Static migration; `BakedLightingComponent` (mode, bake settings,
   output); `LightmapAtlas`/file IO; Scene accessors. Tests: persistence, migration, file round trip.
2. **UV2**: planar chart unwrapper + packer; import option; validity checks. Tests: no overlaps, inside [0,1], padding.
3. **Baker**: extract `BakeRayScene`; lightmap stage (rasterise, direct from Baked lights, mask for the Mixed sun,
   multi-bounce indirect with sky and emission, dilation, denoise); probes gain Baked lights' direct light; write file;
   finalise into the component. Tests: lit plane, occluder shadow in mask, bounce colour, dilation.
4. **Renderer**: per-draw rect plumbing, atlas binding, shader sampling on forward/bindless paths, mixed shadow mask,
   baked light exclusion. Tests: Vulkan and OpenGL render checks agree.
5. **Editor**: light Mode, Lighting settings section, bake settings, menu and status; docs.
6. **Validation** on RocketLeg.

## Deferred (designed for, not built)

Terrain lightmaps (terrain UV2 is layer data: needs a UV0 mapping flag), instanced/foliage lightmaps (per-instance
rects in instance records), more shadow-mask channels for local lights, directional lightmaps, GPU baking, multi-page
atlases (needs float texture arrays in the RHI), seam stitching across charts.

## Validation (2026-11-26)

- RocketLeg copy (static GLB meshes given Generate Lightmap UVs, Ambient Light Volume active): `--bake-lighting` baked
  23 draws into a 1339x1572 atlas at 4 texels/m plus 4001 probes in 21 s (16 threads). With the sun Mixed, renders keep
  realtime car and ball shadows; with the sun Baked, the floor is lit from the lightmap and probes, and moving objects
  lose their realtime sun shadows, as designed.
- The change exposed a build gap: shader modules including BasicLit.slang outside the BasicLit/Glass names (GeometryDepth,
  DeferredLocalLights, DirectionalShadowMask) did not rebuild with it, so streamed draws read a stale object stride. Fixed
  in engine/render/CMakeLists.txt.
- Built-in cubes scaled into floors got uneven texel density (RocketLeg's 120x82 m floor: about 1.9 x 2.7 texels/m on
  its top face at 4 requested). Fixed the same day with per-instance UV2: static built-in shapes stretched unevenly
  draw and bake with a copy unwrapped at their world scale (`LightmapUvSettings::positionScale`,
  `Mesh::CreateScaleFittedLightmapUvs`, `MeshComponent::GetLightmapMesh`, opted in by
  `MeshConfig::lightmapUvsFollowScale`). Rebaked: the floor's top face gets 335x485 texels (4.1 texels/m both ways)
  and the atlas shrank from 1339x1572 to 1374x1268, since thin sides no longer take a sixth of each layout. The rect
  text version became 2, so bakes against the old layouts load as no bake instead of sampling the wrong texels.
