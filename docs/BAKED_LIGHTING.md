# Baked lighting: lightmaps, light modes and the scene lighting mode

Static geometry can take its shadows and bounce light from lightmaps baked on the CPU, while characters and other moving
objects keep realtime lighting and cast realtime shadows. Three settings decide what is baked and what stays realtime:

| Setting | Where | Values |
| --- | --- | --- |
| **Lighting Mode** (per scene) | Scene > Lighting > Lighting Mode, or the scene's **Baked Lighting** object | **Realtime**: lightmaps are ignored and every light is realtime. **Baked**: static meshes use their lightmaps and each light follows its Mode. |
| **Mode** (per light) | Light inspector | **Realtime**, **Mixed** or **Baked** (below) |
| **Mixed Shadows** (per scene) | Scene > Lighting > Mixed Shadows, or the **Baked Lighting** object | **Moving Objects Only (Shadowmask)** (new scenes) or **All Realtime** ([below](#mixed-shadows)) |
| **Static** (per mesh) | Mesh inspector | Static meshes with lightmap UVs are lightmapped; every static mesh occludes and bounces light in the bake. |

## Light modes

| Mode | Static (lightmapped) surfaces | Moving objects | Shadows from moving objects |
| --- | --- | --- | --- |
| **Realtime** | Realtime light and shadows; nothing baked | Realtime | Yes |
| **Mixed** | Realtime direct light; static shadows baked (Mixed Shadows decides whether static objects also cast realtime ones); bounce light baked | Realtime direct light and shadows; bounce from the ambient volume | Yes |
| **Baked** | Direct and bounce light from the lightmap, at no realtime cost | Direct and bounce light from the ambient volume | No |

- Use **Mixed** for the sun and any light that must cast character shadows. Static geometry still gets soft baked
  shadows beyond the realtime shadow distance, and keeps its static shadows when a graphics preset turns realtime
  shadows off. Specular highlights and normal maps stay realtime.
- Use **Baked** for fill and decorative lights. They cost nothing at runtime, but moving objects receive them only through
  an **Ambient Light Volume**, so bake one around the playable space, and they cast no realtime shadows.
- Modes only matter while the scene's Lighting Mode is **Baked** and a bake is loaded. Otherwise every light renders in
  realtime, so an unbaked scene is never dark.
- Scenes from before light modes load their lights' old **Static** flag as Mixed (a static light baked its bounce and
  stayed realtime) and non-static lights as Realtime.

The baked static shadow mask belongs to the scene's first directional light when it is Mixed and casts shadows. It is
used only while that light shines from the direction it was baked from (within about a degree): turning the sun, or a
physical sky's moon at night, switches it off until the next bake. Mixed point, spot and rect lights bake their bounce
and keep realtime shadows only.

## Mixed shadows

A Mixed sun shadows static geometry in one of two ways:

| Mixed Shadows | Static objects' shadows | Moving objects' shadows | Cost |
| --- | --- | --- | --- |
| **Moving Objects Only (Shadowmask)** | Baked: the lightmap's shadow mask on lightmapped surfaces, the ambient volume's sun visibility on everything else (moving objects, terrain, foliage) | Realtime, onto everything | The realtime shadow is filtered only where a moving object's shadow can fall, and static objects stay out of the shadow maps |
| **All Realtime** | Realtime, sharpened by the bake beyond the realtime shadow distance | Realtime | Every pixel filters the realtime shadow every frame |

New scenes use **Moving Objects Only**. Scenes saved before the setting existed load as **All Realtime**, so they look
as they did. The Baked Lighting inspector and the Mixed Shadows menu say whether shadowmask is working, or what it is
waiting for: a bake, a Mixed sun that casts shadows, or a rebake after the sun turned.

Trade-offs of **Moving Objects Only**:

- Static shadows are as sharp as the lightmap. Raise **Texels Per Unit** for crisper edges.
- Static objects' shadows on moving objects and unlightmapped surfaces come from the ambient volume's probes, so they
  are soft and coarse (the probe spacing). Bake an Ambient Light Volume around the play space.
- The sun's realtime contact shadows are off; moving objects keep their shadow maps.
- Static objects stay in the realtime shadow maps while something else reads them there: a shadowed spot light with
  virtual shadow maps, volumetric fog, or water. The profiler's "RHI shadowmask" line says when this happens.
- Like the shadow mask, it applies only while the sun shines from where it was baked.

## Baking

1. Mark level geometry **Static**.
2. Give static meshes lightmap UVs (below).
3. Set each light's Mode. Add a **Physical Sky** for sky light, and an **Ambient Light Volume** around the playable
   space for moving objects.
4. **Scene > Lighting > Bake Lighting.** The first bake adds a **Baked Lighting** object holding the settings. The status
   bar shows progress (`Baking lightmaps: 42% ...`), then what was baked and anything skipped, for example meshes
   without lightmap UVs.

The atlas is saved to `Assets/Lightmaps/<scene>.plutolightmap`. The scene references it and builds always include it.
**Scene > Lighting > Clear Baked Lighting** clears lightmaps and probes. **Bake Settings...** can turn lightmaps off for a
bake.

Headless (CI or tools): `PlutoGERenderSceneShot <project> <scene> --bake-lighting <output.plutoscene>` bakes lightmaps
and ambient volumes and writes the atlas beside the output scene.

### Lightmap UVs

A lightmapped mesh needs a secondary UV set (`uv2`) in which every triangle has its own texels, inside 0..1, with no
overlaps. Each submesh is lightmapped as its own draw and gets its own layout.

- **Authored**: a second UV set (glTF `TEXCOORD_1`, FBX/OBJ channel 1) is used as it is.
- **Generated, model pipeline**: Content Browser > model > Import Settings > **Generate lightmap UVs**, then Apply and
  Reimport.
- **Generated, model files referenced directly** (older scenes, `project://Models/x.glb` on a mesh): the mesh
  inspector's **Generate Lightmap UVs** appears on static meshes without lightmap UVs.
- **Built-in shapes**: the cube has a hand-made layout; quad, plane, sphere and cylinder get generated ones. A static
  built-in shape stretched unevenly (a cube scaled into a floor or wall) draws and bakes with its own copy, unwrapped
  at its world scale, so every face gets the requested texel density
  (`MeshComponent::GetLightmapMesh`, `Mesh::CreateScaleFittedLightmapUvs`). Only scenes with a Baked Lighting object
  use copies; a copy is refitted when the stretch changes by about 2%, which needs a rebake.

Generated layouts come from a planar chart unwrapper (`render/LightmapUvs.h`): triangles whose normals stay within 50°
of each other form a chart, each chart is projected onto its mean plane in world units, turned to its smallest
bounding box and packed with gutters. Hard-surface and architectural meshes unwrap well; dense organic meshes produce
many small charts.

## Debugging

The viewport's debug view menu (Post Process, Quadrants, ... ) has two baked lighting views:

| View | Shows |
| --- | --- |
| **Lightmap** | The lightmap's baked light alone, untextured and through a simple tone curve. Surfaces without lightmap texels are dark grey. Look here for noise, light leaking under walls, dark seams between charts and blocky low-resolution shadows. |
| **Lightmap Coverage** | **Green**: lightmapped. **Yellow**: has lightmap texels but the scene's Lighting Mode is Realtime. **Red**: a static mesh the bake left out (no lightmap UVs, unusable UVs, or not baked since it became static). **Grey**: lit without lightmaps (moving objects, terrain, scattered foliage), which take baked light from ambient volumes. |

What the Lightmap view holds depends on the lights' modes: with a Mixed sun it is the bounce and sky light (the sun's
direct light stays realtime); with a Baked sun it includes the sun's direct light and shadows. To see the baked
lighting as players will, set the sun to Baked temporarily: moving objects then lose their realtime sun shadows.

Pixels are written by `BasicLit.slang` into the albedo target (`LightmapSampling.slang` `lightmapDebugColor`, chosen by
`geometryParameters.y`) and shown by `DisplayOutput.slang`. Static meshes mark their draws with
`LightmapRect::staticReceiver`, which reaches the shader as z = -1 in the draw's lightmap constant.

## Settings (Baked Lighting object)

| Setting | Default | Meaning |
| --- | --- | --- |
| Texels Per Unit | 4 | Lightmap texels per metre. The bake lowers it until the atlas fits Max Atlas Size; the status bar reports what it used. |
| Max Atlas Size | 2048 | Width and height limit of the single atlas. |
| Chart Padding | 2 | Empty texels around each draw's charts, filled by dilation so bilinear reads never pick up a neighbour. |
| Direct Shadow Samples | 16 | Shadow rays per texel for soft (Shadow Softness) directional shadows. |
| Indirect Samples | 128 | Gather rays per texel. More samples, less noise. |
| Sky Visibility Samples | 64 | Rays per texel for the sky visibility layer, which occludes lightmapped surfaces' sky reflections and relights reflection captures without the ambient volumes. 0 bakes no layer: lightmapped surfaces then take those terms from the volumes, at a cost. |
| Reflection Occlusion | 1 | How strongly the sky visibility layer occludes sky reflections (0 to 1), as an ambient volume's setting does. Applies without a rebake. |
| Bounces | 2 | Diffuse bounces. 1 is light reflected once off a surface. |
| Bounce Strength | 1 | Scales every bounce. 1 is physically based. |
| Denoise | on | Edge-aware filtering of the gathered light within each draw. |

## How it works

**Bake** (CPU, background thread; `engine/scene/src`):

- `bake/BakeKernel` is the ray kernel the probe and lightmap bakes share. It holds the static triangles with their
  materials, a BVH, readable albedo textures, the Mixed and Baked lights, and the sky (`BakeEnvironment`).
- `lightmap/LightmapBake` groups static mesh triangles by draw (entity and submesh). It sizes each draw from its world
  area, packs the draws with a skyline packer, and rasterises their uv2 triangles into texels, including texels a
  triangle only grazes.
- Each texel integrates three things:
  - the Baked lights' direct light, with soft shadow rays;
  - the Mixed sun's visibility, for the mask;
  - with Sky Visibility Samples above 0, which directions reach the sky: rays spread evenly over the sphere (the probe
    bake's set, turned per texel), those below the surface blocked, reduced by `SkyVisibilityAccumulator` to the same
    L1 form the probes store;
  - cosine-weighted, stratified gather rays that read sky radiance when they escape. At surfaces they hit, they add that
    surface's emission and the direct light of every Mixed and Baked light, then bounce on.
- The gathered light is denoised edge-aware within each draw, and each draw is dilated into its padding.
- Probes (`BuildProbeVolume`) add the Baked lights' direct light, so moving objects receive it, and record how much of
  the Mixed sun reaches each probe (`BakedProbeVolume::sunVisibility`, soft shadow rays), for shadowmask. The sun is the
  one whose static shadow the lightmap's mask holds (`FindShadowMaskLight`).

**Storage**:

- `BakedLightingComponent` owns the lighting mode, settings and bake output: the atlas, each draw's rect and the mask's
  light direction.
- The atlas is a `.plutolightmap` file (`LightmapFile.h`): versioned, one or more half-float RGBA layers sharing the
  charts (`LightmapAtlas::layers`): `IrradianceShadowMask`, then optionally `SkyVisibility`. Version 2 lists the layers;
  version 1 files load as one layer. New layer kinds append a `LightmapEncoding` and a slot in `LightmapLayerOrder`.
- Rects are stored in the scene as compact text.
- Ambient volumes store their probes as versioned text; version 4 adds the sun's visibility and its direction. Older
  versions still load, without it.
- Meshes look their rect up when they rebuild render commands, and a new bake marks them dirty.

**Rendering**:

- Each draw carries its rect (`RenderCommand::lightmap`, then `BasicDraw::lightmap`), which reaches the shader in its
  object constants: bound, streamed, or the bindless instance record.
- The atlas is one RGBA16F texture bound per pass (set 0 binding 42, OpenGL unit 42). `BuildSceneLighting` decides whether baked
  lighting is active, the sun's mode and whether the mask applies.
- `BasicLit.slang` (`LightmapSampling.slang`) uses the lightmap in place of the ambient grid and sky diffuse light,
  takes the darker of the realtime shadow and the mask, and leaves out a Baked sun.
- Lightmapped pixels write indirect alpha 0.375, so DDGI and SSGI keep the baked light; SSAO still applies.
- Baked local lights are not submitted.
- A Baked sun costs no realtime shadow work: `ResolveBakedLighting` clears `BasicLighting::shadowsEnabled`, which skips its
  virtual shadow maps or cascades, the directional shadow mask pass and its contact shadow (local lights keep theirs).
  Realtime GI (VCT injection, ray-traced DDGI) leaves it out too (`BakedLightingFrame::DirectionalBaked`), since its
  bounce is already in the lightmaps and probes.
- The renderer stacks the layers in one texture (`StackLightmapLayers`, `LightmapSampling.slang`
  `lightmapLayerCoordinate`, which keeps reads half a texel inside their layer), so a new layer needs no binding.
- With the sky visibility layer, lightmapped surfaces take sky reflection occlusion and capture relighting from their
  texels (BasicLit `lightmapEnvironment`), everywhere rather than only inside ambient volumes, and read nothing from the
  volumes: their pipelines compile the probe grid out (`SurfaceFeatures.h` `DrawSurfaceFeatures`). Without it they
  defer the grid's reflection terms to the screen-space resolve when it applies them (BAKED_ENVIRONMENT_LIGHTING.md,
  "Cost"); a forward grid sample per lightmapped pixel would cost more than the rest of their shading. Otherwise the
  resolve (`DeferredProbeIrradiance`) runs only over 32-pixel tiles
  touched by unlightmapped opaque draws (`DeferredProbePixelRegion`, per instance through
  `BasicDraw::localBoundingSphere`), and not at all when there are none. Wind-animated draws, detail grass and draws
  without bounds fall back to the whole view. The profiler shows the covered pixels as "RHI deferred probe resolve".
- Shadowmask (`render/src/Shadowmask.h`): `ResolveBakedLighting` turns it on when the setting asks for it and the mask
  applies. Then:
  - `RealtimeShadowRegion` sweeps each moving caster's bounds along the light until they leave the receivers' bounds,
    and marks the screen tiles that sweep covers (`ScreenTileGrid`). The directional shadow mask pass filters only
    those tiles and clears the rest to lit (Vulkan; OpenGL filters in the surface shader, as always).
  - `PlanShadowmask` decides whether static casters (`BasicDraw::staticGeometry`, from the Static flag) leave the
    cascades and virtual shadow maps (`CastsRealtimeDirectionalShadow`).
  - `BasicLit.slang` takes the darker of the realtime shadow and the static one: the lightmap's mask, or
    `probeSunVisibility` (`AmbientProbeSampling.slang`, the probes' sun visibility in SH row 6's .w).
  - `SelectLightVisibilitySlots` leaves out the sun's contact slot.
  - `DescribeMixedShadows` gives the editor the same decision with the reason it is off.

**Performance.** What baking saves depends on the lights' modes and Mixed Shadows. With **All Realtime**, a Mixed sun
keeps all of its realtime cost (shadow maps, shadow mask, contact shadows, direct shading): only the ambient term
changes source. **Moving Objects Only** filters the realtime shadow over the screen near moving objects only and skips
the sun's contact shadows; in RocketLeg's chase view that is 23% of the pixels. A Baked sun saves the most, at the
cost of moving objects' realtime sun shadows.

## Limits and next steps

- **One atlas page.** Very large static worlds lower their texel density to fit Max Atlas Size. Multiple pages need float
  texture arrays in the RHI.
- **Non-uniformly scaled imported meshes.** Built-in shapes refit their layout to their scale (above); imported meshes
  keep the layout unwrapped at import, so a stretched instance gets uneven texel density. Model them at size, or let
  their layout opt in through `MeshConfig::lightmapUvsFollowScale` (meshes without LODs or a skeleton).
- **Terrain, foliage and instanced models** are not lightmapped. They occlude and bounce light in the bake and receive
  light from ambient volumes. Terrain's uv2 holds layer weights, so it needs a mapping of its own.
- **One shadow-mask channel** (the sun). Mixed local lights keep realtime shadows only; Mixed Shadows applies to the sun.
- **Shadowmask and scattering particles**: particles lit by the sun read the realtime shadow maps, which may hold moving
  objects only, so static objects do not shade them.
- **Non-directional lightmaps.** Normal maps on lightmapped surfaces see a flat baked bounce; direct light from Mixed
  lights keeps them.
- **Chart seams** are not stitched, so a seam between charts can show a small discontinuity.
- **Time of day.** Lightmaps bake the sky and sun as they are. With a moving sun, use Realtime lighting mode, or Mixed
  lights with a Live ambient volume.
- **Sun colour under a physical sky**: bakes use a light's own colour and intensity. Realtime shading takes the sun's colour
  through the physical sky's atmosphere, so a Baked sun can differ slightly in colour from the realtime one (the probe
  bake has always worked this way).
- **Voxel cone traced GI** adds to the baked light rather than replacing it. Turn it off with baked lighting. DDGI and
  SSGI leave lightmapped surfaces alone.

## Tests

- `PlutoGEBakedLightingTests` (CPU) covers:
  - generated lightmap UVs: inside 0..1, no overlaps, reasonable use of the layout;
  - scale-fitted layouts for stretched built-in shapes (face area follows world area; only static meshes in scenes
    with baked lighting), and a stretched floor baking at the requested texel density;
  - skyline packing;
  - the lightmap file round trip and rejection of truncated files;
  - light mode migration;
  - Baked Lighting persistence;
  - a Mixed sun's static shadow in the mask;
  - a Baked sun's direct light (3/π on open ground);
  - reports of meshes without lightmap UVs;
  - shadowmask: the probes' sun visibility under and beside a box, the renderer's decision and the editor's status
    (including after the sun turns), and Mixed Shadows persistence with older scenes loading as All Realtime.
- `PlutoGEShadowmaskRenderTests` (CPU) covers the screen tile grid (disjoint rectangles, eye-plane and off-screen
  cases), the realtime shadow region of moving casters, when static casters leave the shadow maps, and the sun's
  contact slot.
- `PlutoGESceneEditHistoryTests` round-trips ambient volume bakes, including version 4's sun visibility.
- `PlutoGEVulkanRhiTests` checks that streamed and bound recording still match, with the new object constants.
- `PlutoGEVulkanRhiTests` and `PlutoGEOpenGLRhiTests` (`--lightmap-debug-views` runs only this check) render a
  lightmapped, a missing and a moving quad through both debug views (`tests/LightmapDebugViewChecks.h`). It is also the
  only render check that samples a lightmap atlas, so it covers the atlas binding on both backends.
- `PlutoGERenderSceneShot --bake-lighting`, then a render of the saved scene, checks lightmaps end to end.
