# Baked lighting: lightmaps, light modes and the scene lighting mode

Static geometry can take its shadows and bounce light from lightmaps baked on the CPU, while characters and other moving
objects keep realtime lighting and cast realtime shadows. Three settings decide what is baked and what stays realtime:

| Setting | Where | Values |
| --- | --- | --- |
| **Lighting Mode** (per scene) | Scene > Lighting > Lighting Mode, or the scene's **Baked Lighting** object | **Realtime**: lightmaps are ignored and every light is realtime. **Baked**: static meshes use their lightmaps and each light follows its Mode. |
| **Mode** (per light) | Light inspector | **Realtime**, **Mixed** or **Baked** (below) |
| **Static** (per mesh) | Mesh inspector | Static meshes with lightmap UVs are lightmapped; every static mesh occludes and bounces light in the bake. |

## Light modes

| Mode | Static (lightmapped) surfaces | Moving objects | Shadows from moving objects |
| --- | --- | --- | --- |
| **Realtime** | Realtime light and shadows; nothing baked | Realtime | Yes |
| **Mixed** | Realtime direct light; the shadow is the darker of the realtime shadow and the baked static shadow; bounce light baked | Realtime direct light and shadows; bounce from the ambient volume | Yes |
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

## Settings (Baked Lighting object)

| Setting | Default | Meaning |
| --- | --- | --- |
| Texels Per Unit | 4 | Lightmap texels per metre. The bake lowers it until the atlas fits Max Atlas Size; the status bar reports what it used. |
| Max Atlas Size | 2048 | Width and height limit of the single atlas. |
| Chart Padding | 2 | Empty texels around each draw's charts, filled by dilation so bilinear reads never pick up a neighbour. |
| Direct Shadow Samples | 16 | Shadow rays per texel for soft (Shadow Softness) directional shadows. |
| Indirect Samples | 128 | Gather rays per texel. More samples, less noise. |
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
  - cosine-weighted, stratified gather rays that read sky radiance when they escape. At surfaces they hit, they add that
    surface's emission and the direct light of every Mixed and Baked light, then bounce on.
- The gathered light is denoised edge-aware within each draw, and each draw is dilated into its padding.
- Probes (`BuildProbeVolume`) add the Baked lights' direct light, so moving objects receive it.

**Storage**:

- `BakedLightingComponent` owns the lighting mode, settings and bake output: the atlas, each draw's rect and the mask's
  light direction.
- The atlas is a `.plutolightmap` file (`LightmapFile.h`): versioned, half-float RGBA in the `IrradianceShadowMask`
  encoding.
- Rects are stored in the scene as compact text.
- Meshes look their rect up when they rebuild render commands, and a new bake marks them dirty.

**Rendering**:

- Each draw carries its rect (`RenderCommand::lightmap`, then `BasicDraw::lightmap`), which reaches the shader in its
  object constants: bound, streamed, or the bindless instance record.
- The atlas is one RGBA16F texture bound per pass (set 0 binding 39). `BuildSceneLighting` decides whether baked
  lighting is active, the sun's mode and whether the mask applies.
- `BasicLit.slang` (`LightmapSampling.slang`) uses the lightmap in place of the ambient grid and sky diffuse light,
  takes the darker of the realtime shadow and the mask, and leaves out a Baked sun.
- Lightmapped pixels write indirect alpha 0.375, so DDGI and SSGI keep the baked light; SSAO still applies.
- Baked local lights are not submitted.

## Limits and next steps

- **One atlas page.** Very large static worlds lower their texel density to fit Max Atlas Size. Multiple pages need float
  texture arrays in the RHI.
- **Non-uniformly scaled imported meshes.** Built-in shapes refit their layout to their scale (above); imported meshes
  keep the layout unwrapped at import, so a stretched instance gets uneven texel density. Model them at size, or let
  their layout opt in through `MeshConfig::lightmapUvsFollowScale` (meshes without LODs or a skeleton).
- **Terrain, foliage and instanced models** are not lightmapped. They occlude and bounce light in the bake and receive
  light from ambient volumes. Terrain's uv2 holds layer weights, so it needs a mapping of its own.
- **One shadow-mask channel** (the sun). Mixed local lights keep realtime shadows only.
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
  - reports of meshes without lightmap UVs.
- `PlutoGEVulkanRhiTests` checks that streamed and bound recording still match, with the new object constants.
- `PlutoGERenderSceneShot --bake-lighting`, then a render of the saved scene, checks lightmaps end to end.
