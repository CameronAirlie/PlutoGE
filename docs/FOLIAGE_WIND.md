# Foliage wind

Grass, flowers, shrubs and trees placed with a `FoliageComponent` sway in the scene's wind. One `WindComponent` sets
the wind for the whole scene. Each foliage type sets how it answers: stiffness, flutter, bend height and pivot
height. The vertex shader deforms the plants in every pass that draws them, so their shadows sway with them.

This is work package E1 of the [jungle scene plan](JUNGLE_SCENE_PLAN.md). The [Wind Fields sample](../samples/WindFields/README.md)
shows it on a procedural grassland.

## Quick guide

1. **Add wind to the scene.** Select any entity, open **Add Component > Environment > Wind**, and set:
   - **Direction:** where the wind blows toward, in degrees. 0 is +X; 90 is +Z.
   - **Speed:** in m/s. Plants bend furthest at 20 m/s and above.
   - **Gustiness:** 0 is a steady wind. 1 swings between calm and twice the mean strength.
   - **Gust Size:** the distance between gust fronts. The fronts roll downwind across fields at the wind speed.

   The first enabled Wind component on an active entity drives the scene. Without one the air is calm, and nothing
   moves.
2. **Make a foliage type sway.** Select the entity with the `FoliageComponent`, select the type, and open **Wind**.
   Tick **Sway In Wind**, then pick a preset: **Grass**, **Flowers**, **Shrub** or **Tree**. Presets size themselves
   to the mesh's height. Tune from there:
   - **Stiffness:** 0 bends fully (grass); 1 does not sway (trunks). Stiffer plants also sway faster.
   - **Flutter:** fast, small motion of leaves and blade tips.
   - **Bend Height:** the mesh-space height (above the mesh origin) where the sway reaches its full lean. Set it to
     about the plant's height. **Fit** sets it from the mesh.
   - **Pivot Height:** geometry at or below this height stays put. Use it for the base of a trunk.

   When a tree's trunk and crown are separate foliage types, give both the same placements and the same wind settings
   so they bend together.
3. **Script it** with `WindComponent` in C#: `Direction`, `Speed`, `Gustiness` and `GustSize`. Raising `Speed` and
   `Gustiness` over a few seconds makes a storm.

Wind animates in Play mode and in the editor viewport, where the scene clock also runs. `PlutoGERenderSceneShot
--time <seconds>` captures the pose at a given time.

## How it moves

`foliageWindOffset` in `FoliageWind.slang` computes each vertex's offset. It depends only on the vertex's mesh
position, the instance transform, the packed parameters and time, so every pass computes the same offset:

- **Sway.** The plant leans downwind and oscillates about that lean.
  - The lean follows the gust passing the plant's root, so a whole plant moves as one piece and fields ripple as gusts
    roll through.
  - Each plant has its own phase, hashed from its root position with integer arithmetic. Every pass computes the same
    value.
  - The lean grows with the square of the height above the pivot (in bend heights, clamped at 1.5), so roots stay
    planted.
- **Flutter.** A small, fast motion that varies per vertex.
- **Length keeping.** Leaning geometry drops, so stems keep roughly their length instead of stretching.

**Gusts** are two waves that travel downwind at the wind speed (`SceneWindGust`). The second, shorter wave is skewed
across the wind, so gust fronts are not straight lines.

**Strength** is the speed over 10 m/s, clamped to 2. Because strength is clamped, the displacement is bounded for any
wind. `FoliageWindMaxDisplacement` gives that bound; culling pads bounds with it without knowing the scene's wind.

The model's constants live in `FoliageWindLayout.h`, which C++ and Slang share. `FoliageWind.cpp` is the CPU
reference of the shader functions, and `FoliageWindTests` checks the properties both sides rely on.

## Architecture

Each layer depends only on the one below it:

| Layer | Types | Role |
| --- | --- | --- |
| Scene | `scene::WindComponent`, `FoliageTypeAsset::wind` | Authoring, serialization, editor and C# exposure. |
| Extraction | `render::BuildSceneWind` (SceneEnvironment.cpp) | The first enabled wind becomes `BasicLighting::wind`. |
| Render data | `SceneWindSettings`, `FoliageWindResponse` on `RenderCommand` and `BasicDraw` | Plain values: no scene types reach the renderer. |
| Frame | `FoliageWindFrame` (`BasicRenderer::m_foliageWind`) | This frame's wind and clock. |
| GPU block | `FoliageWindParameters` (64 bytes) | Packed by `PackFoliageWind`. All zero when a draw does not move. |
| Shader | `FoliageWind.slang`: `foliageWindPosition` | One function that every pass calls. |

`FoliageWindDraws.h` classifies draws in one place, and the renderer's rigid-geometry fast paths use these predicates:

- `GraphDeformsVertices` (a shader graph's vertex offset).
- `HasFoliageWind`.
- `DeformsVertices`, which is either of the two.

A future vertex deformation (cloth on the GPU, vertex animation textures, interactive bending) belongs in
`DeformsVertices`, so every pass picks it up.

### Passes

| Pass | Where the block travels | Notes |
| --- | --- | --- |
| Forward, G-buffer | `MaterialParameters.foliageWind` (BasicLit.slang) | Instanced and single-object paths share `transformSurfaceVertex`. |
| Motion vectors | the same block, at the previous frame's time | Temporal filters see the sway as motion and do not smear it. |
| Depth prepass | the same block, through the standard coverage shaders | Material-free opaque depth cannot read it, so wind draws use alpha coverage. Graph draws use the full layout. |
| Cascaded, point and spot shadows | `ShadowMaterial.foliageWind` (DirectionalShadow.slang) | Excluded from batched opaque shadows (`OpaqueShadowBatches::Eligible`). |
| VSM | `DrawParameters` and `BatchedDraw` (VirtualShadowRaster.slang) | Page and receiver vertices both deform. |
| Bindless GPU-driven opaque | none | Wind draws take the regular path (`GpuDrivenOpaque::Eligible`). |
| Impostor cards | none | Cards are distant stand-ins and stay still (`MakeImpostorDraw`). |
| Voxel GI, and its shadow cascades | none | GI lights the rest pose, so swaying never forces a re-voxelization. |
| Hardware ray-traced DDGI | none | BLAS geometry is static. |
| Occlusion | none | Wind draws are occludees, never occluders. |

### Caches and invalidation

The rule is that a calm scene changes nothing. Draws with wind enabled pack zero constants in calm air, keep their
persistent material blocks, and render bit-identically to draws without wind; the GPU checks verify this. When a draw
is swaying:

- **Constant blocks.** Material blocks become per-frame transient blocks, keyed by the animation state, as time-driven
  shader graphs do. Shadow material blocks do the same.
- **Shadow signatures.** These hash the packed parameters, which include time, for the cascaded, point and VSM paths.
  Swaying casters therefore redraw their cascades and VSM pages every frame. Wind-free casters stay cached.
- **Shadow sway distance.** VSM pages farther than the wind's **Shadow Sway Distance** from the camera (default
  30 m, `SceneWindSettings::shadowSwayDistance`, 0 sways everywhere) hold their rest pose, under the draw's signature
  without the wind parameters, so they stay cached. The test is per page (`pageSways` in `VirtualShadowCompute`) and
  measured in the light's view, because a page is the unit of cost: one tree can reach hundreds of pages. A page
  crossing the distance redraws once. Coarse levels always hold the rest pose, and cascades sway at every distance.
  See [VSM in dense foliage](vsm-dense-foliage.md).
- **Shadow bounds.** VSM pads caster bounds with `FoliageWindMaxDisplacement(wind, response, localTop)`: the bound
  under the current wind's strength and gustiness, for the geometry's actual height. It is far tighter than the
  any-wind bound that culling uses (about 3 m against 30 m for Verdant's canopy at 6 m/s), and VSM rebuilds its chunk
  bounds whenever the wind settings change.
- **Draw preparation.** `RenderCommand::foliageWind` is part of the draw-preparation cache key
  (`RhiDrawPreparationCache`), the retained-scene change check and surface equality (`SameBasicDrawSurface`), so
  editing a type's wind rebuilds its draws.

### Bounds

Bounds are padded by `FoliageWindMaxDisplacement`, scaled by each instance's vertical scale:

- Foliage cluster bounds (`FoliageComponent::RebuildRenderCommandCache`). These feed submission culling, shadow
  culling and shadow distance.
- Per-instance visibility in CPU binning (`InstanceBinning::Bin`). Only visibility widens; LOD selection keeps the rest
  radius, so wind never changes detail.
- `BasicDraw::maxInstanceRadius`.

## Data and compatibility

The scene keys are per type: `Type.N.WindEnabled`, `Type.N.WindStiffness`, `Type.N.WindFlutter`,
`Type.N.WindBendHeight` and `Type.N.WindPivotHeight`. A `.plutofoliage` asset (PLUTOFOLIAGE 1) adds two records:

```text
WIND_ENABLED	1
WIND_RESPONSE	0.85	0.35	8.5	1.7
```

The `WIND_RESPONSE` fields are stiffness, flutter, bend height and pivot height.

Older scenes and assets have none of these keys and load with wind off, so they do not move until a type opts in.
Values are sanitized on load and in `SetTypeWind`.

`WindComponent` serializes `Direction`, `Speed`, `Gustiness`, `Gust Size` and `Shadow Sway Distance` (scenes saved
before it load with 30 m). Malformed values keep their defaults,
and out-of-range values are clamped.

## Performance

- **Calm scenes, or scenes without wind,** cost nothing extra: the shader branch is uniform and skipped.
- **Swaying foliage** adds a few dozen arithmetic operations per vertex in each pass it draws in (no texture reads),
  and one 64-byte constant block per draw per frame.
- **Shadows dominate the cost.** Every page a swaying caster touches is redrawn each frame. Large fields of ground
  cover should use contact shadows instead: turn off **Cast Shadows** on the grass component. Wind Fields does this for
  its 70,000 grass clumps on the Vega 8 target, and keeps shadow-map shadows for trees and shrubs.

### Measured on Wind Fields (2026-11-05, Vega 8, 1080p)

The content fixes below were measured with the editor open, which inflates GPU timings (see "Contention" in
[rendering-performance.md](rendering-performance.md)); their direction holds, but compare absolute numbers only with
runs made alone.

| Change | Effect |
| --- | --- |
| Grass blades single-faced with a one-sided material, instead of duplicated back faces | Coincident front and back triangles both pass the shaded pass's depth-equal test, so every grass pixel was lit twice. Removing them took about a third off the frame. |
| LOD chains for foliage meshes (`MODEL_OPTIONS 15`: generated and vegetation LODs) | Opaque triangles 4.3 M → 0.2 M. Without a chain every clump draws all its blades at any distance. |

Foliage that references a source model directly (`project://…glb`) now imports it with the model's own import settings
from its `.plutometa`; before, it used the importer's defaults, so Content Browser settings such as **Vegetation LODs**
had no effect.

Measured alone, after the renderer's surface specialisation and cascade caster LODs (rendering-performance.md, "Fourth
pass"), the GPU frame was 12.4 ms at Low, 17.7 ms at Medium and 27.7 ms at the default preset. The fifth pass (the
directional shadow mask, the interleaved cloud trace and still shadow pages beyond the sway distance) takes the
default preset to 20.1 ms.

## Limitations and next steps

- **Normals do not bend.** Lighting uses the rest normal, which is invisible at grass and leaf scales but slightly flat
  on strongly bent trunks.
- **GPU instance culling** (`GpuInstanceCulling`, off by default) does not pad instance bounds yet, so swaying
  instances at the screen edge can cull a few pixels early.
- **Impostors** do not sway. Frame-blended impostors could offset their cards by the root's lean.
- **Other systems still have their own wind settings.** Clouds, particles, cloth, fog and the ocean do not read the
  scene wind yet. `SceneWindVelocity` and `BuildSceneWind` give them the same field, gusts included, when they adopt it.
- **No interaction.** Characters do not push grass aside yet. An interaction field would be another input to
  `foliageWindOffset`.

## Tests

- `PlutoGEFoliageWindTests` covers the CPU side:
  - Calm air and disabled types pack zero constants.
  - Roots stay put, and the displacement bound holds for random winds, plants, scales and times.
  - Plants lean downwind, softer plants further, and gusts stay in range and travel downwind.
  - `WindComponent`, per-type and `.plutofoliage` round trips, including legacy data and malformed values.
  - Scene extraction and its cache.
  - Render-command propagation and bounds padding.
- `PlutoGEVulkanFoliageWindTests` and `PlutoGEOpenGLFoliageWindTests` (`--foliage-wind`) render real blades through
  every pass:
  - A calm wind-enabled draw renders bit-identically to a draw without wind.
  - Tips lean downwind while roots stay put, through the instanced and single-object paths, opaque and masked.
  - Swaying blades keep their coverage, so the depth prepass agrees with the shaded pass.
  - Their shadows move downwind with cascaded shadow maps and VSM.

## Files

- `engine/render/include/PlutoGE/render/SceneWind.h`, `FoliageWind.h`, `FoliageWindLayout.h`
- `engine/render/src/FoliageWind.cpp`, `FoliageWindDraws.h`
- `engine/render/shaders/FoliageWind.slang`, plus its callers: `BasicLit.slang`, `DirectionalShadow.slang` and
  `VirtualShadowRaster.slang`
- `engine/scene/include/PlutoGE/scene/components/WindComponent.h`, `engine/scene/src/components/WindComponent.cpp`
- `engine/scene/include/PlutoGE/scene/FoliageTypeAsset.h` and `FoliageComponent` (`SetTypeWind`)
- `editor/ui/include/PlutoGE/ui/WindInspectors.h`, `editor/ui/src/inspectors/WindInspectors.cpp`
- `engine/scripting/managed/PlutoGE.ScriptCore/WindComponent.cs`
- `tests/FoliageWindTests.cpp`, `tests/FoliageWindRenderingChecks.h`
