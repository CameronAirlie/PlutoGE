# Terrain Material Layers

A terrain can carry up to eight **material layers**: surfaces such as dust, grass, rock and gravel, each with its
own maps and tiling, painted onto the terrain with a brush and blended per texel. Without layers a terrain draws
with its material exactly as before.

## Using layers

1. Select the terrain. In the inspector's **Material Layers** section press **Add Layer**. The first layer covers
   the whole terrain.
2. Open a layer and assign its maps from the project's textures (pick or drag from the Content Browser):

   | Map | Convention | When empty |
   | --- | --- | --- |
   | Albedo | sRGB colour | white |
   | Normal | tangent space, OpenGL (+Y up), like glTF | flat |
   | Roughness | red channel | 1 |
   | Height | red channel, for height blending | 0.5 |

   **Tile Size** is the world size of one repeat of the maps (a 2 m ground scan uses 2). **Tint**, **Normal
   Strength**, **Roughness** (multiplies the map) and **Metallic** shade the layer. **Anti-Tiling** hides the maps'
   repeats (stochastic tiling: each map is sampled twice, from offsets chosen by a slow noise), which matters for small
   scans that would otherwise read as a grid from a distance.
3. Press the round button beside a layer (or choose **Paint > Mode: Layer** in the viewport) and paint with the left
   mouse button. **Radius** (world units), **Opacity** (how much one dab adds) and **Hardness** (how much of the
   radius paints at full opacity before the soft edge) shape the brush. Dabs are spaced along the stroke, so the
   result does not depend on frame rate or mouse speed.
4. **Fill Terrain** gives a whole terrain to one layer. **Height Blend** above 0 lets the taller surface (by the
   layers' height maps) show through transitions, e.g. stones poking out of sand. **Detail Distance** (0 =
   unlimited) is how far from the camera the layers are drawn; beyond it, after a short fade, the terrain shows its
   macro colour, which is much cheaper. **Paint Resolution** sets the weight map size (existing paint is resampled).

Strokes, Fill, Remove, reordering and resolution changes are single undo steps. Painting is saved to the weight map
files when a stroke ends.

## Data

Layers are stored on the TerrainComponent:

| Property | Meaning |
| --- | --- |
| `LayerCount`, `Layer.<n>.Name` | the layers, bottom first |
| `Layer.<n>.AlbedoMap`, `NormalMap`, `RoughnessMap`, `HeightMap` | texture asset references |
| `Layer.<n>.Tint`, `TileSize`, `NormalStrength`, `Roughness`, `Metallic`, `AntiTiling` | shading |
| `WeightMap0`, `WeightMap1` | weight PNGs (layers 0-3 and 4-7 in RGBA) |
| `WeightResolution`, `HeightBlend`, `DetailDistance` | weight map size, height blend range (0..1), layer draw distance (0 = unlimited) |
| `PaintLayer`, `LayerOpacity`, `BrushHardness` | brush settings |

The editor creates weight maps under `Assets/TerrainLayers/` the first time a terrain is painted. A weight map is
an ordinary RGBA8 PNG, oriented like a height map (the image's top row is the terrain's far, +z, edge), so terrain
tools and generators can write them; weights that do not sum to 255 are renormalized on load, and empty texels go
to layer 0.

## Architecture

| Piece | Responsibility |
| --- | --- |
| `scene::TerrainWeightMap` | per-texel layer weights (sum 255), brush painting, layer remove/move, resampling, undo regions |
| `scene::TerrainLayerStack` | the authored layers and weights; publishes a `render::TerrainLayerSet`, CPU-owned weight textures and a 256² macro albedo; loads/saves weight PNGs through injected services |
| `scene::TerrainComponent` | owns the stack, serializes it, paints at world positions, and draws with a private copy of its material carrying the layers (`GetRenderMaterial`) |
| `render::TerrainLayerSet` (`MaterialConfig::terrainLayers`) | what the renderer sees: layer maps and scalars, weight textures, height blend |
| `render::BuildTerrainLayerArrays`, `RhiTerrainLayerCache` | resample the layers' maps into three 2D texture arrays (albedo, normal, surface = roughness R + height G), cached per device by texture identity and revision |
| `BasicLitTerrain.slang` + `TerrainLayers.slang` | the standard material shader with the surface replaced by the layer blend; world-space tiling; selected per draw by `BasicDraw::terrainLayers` |
| `ui::TerrainLayerEditing` | inspector section, paint controls, strokes, weight saving, undo |
| `TerrainComponent` (C#) + `TerrainScriptApi.cpp` | scripts: surface height/normal, layer names, weights and dominant layer under a point, runtime painting (see CSHARP_SCRIPTING.md) |

Notes for extending it:

- **Bindings.** The terrain variant adds five textures (set 1, bindings 20-24) and a parameter block (binding 25).
  OpenGL flattens them to texture units 3-7 and uniform binding 4 (`NormalizeOpenGLShader.cmake`); the RHI slots are
  `BasicRenderer::TerrainTextureSlot` and `TerrainParameterSlot`. Only terrain pipelines declare them.
- **Texture arrays.** `rhi::TextureDescriptor::arrayLayers` creates sampled 2D RGBA8 arrays on Vulkan and OpenGL,
  with mips generated per layer.
- **More layers** means raising `kMaxTerrainLayers` (`TerrainLayerLimits.h`), adding weight-map bindings, and the
  matching constants and loops in `TerrainLayers.slang`.
- **Per-layer features** (detail normals, triplanar rock, distance tiling) belong in `sampleTerrainLayers`; per-layer
  scalars go into `BasicTerrainLayers::tiling`/`tint` and the parameter block, which never rebuild the arrays.
- **The macro albedo** is the terrain material's ordinary albedo, so GI voxelization, lightmap baking and the legacy
  OpenGL renderer see the painted colours without knowing about layers.
- **Surface queries.** `TerrainWeightMap::DominantLayer` gives the layer under a point (footsteps, decals, AI).

- **Painting cost.** Painting patches textures in place: `TerrainLayerStack` records the painted rectangle,
  `render::Texture::UpdateRgba8Region` keeps a history of changed rectangles, and the RHI scene renderer uploads just
  that rectangle with `IRenderDevice::UpdateTexture` (both backends regenerate the mips) instead of re-creating the
  texture and re-preparing materials. The macro albedo is patched the same way.
- **Shading cost.** A layer is two texture fetches (albedo with height in alpha, normal with roughness in alpha), or
  four with anti-tiling. Pixels a single layer covers (weight >= 254/255) take a one-layer path; elsewhere every
  layer's weight is read and the present layers are shaded, capped at the four strongest (`kTerrainShadedLayers`).
  Beyond `DetailDistance` the layers are skipped entirely: the last quarter of that distance fades to the macro
  albedo, which the material already samples.
- **What drives the cost** is how many layers overlap per pixel, so it is mostly an authoring choice. Soft, partial
  regions (e.g. grass painted at 85% over dust) make most of the ground a two- or three-layer blend; giving region
  interiors full coverage and keeping transitions narrow lets most pixels take the one-layer path. Measured on the
  Basin (integrated GPU, 853x480 internal): layers cost ~6 ms of opaque-pass time over a plain painted terrain;
  dropping weights under 25% (12% -> 78% single-layer texels) recovered ~4 ms of it. The detail distance only helps
  views with lots of distant ground: a third-person camera's ground is mostly within 30 m.

Not yet: the legacy (non-RHI) OpenGL renderer draws the macro albedo only.
