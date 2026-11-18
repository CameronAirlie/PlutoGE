# Light occlusion for local lights

Point, spot and rectangle lights without a shadow map used to light everything in their range, through walls, floors
and hillsides. Shadow maps fix that but cost a cube of six renders per light, so a frame affords only a few. In a
procedural world with many torches, campfires and lamps, that leaves most lights leaking.

Three tiers now share the work:

| Tier | What | Lights per frame | Cost |
| --- | --- | --- | --- |
| Shadow maps | Point cube faces and spot virtual shadow pages, cached when casters stand still | 4 points, 4 spots | Renders per face (cached) |
| Traced shadows | A test along the whole segment from each pixel to the light, against the world DDGI keeps | **Traced shadow lights** (8 by default) | One ray (or voxel march) per pixel each light reaches |
| Contact shadows | A short screen-space march towards the light | 3 | A few depth taps per pixel |

Lights get shadow maps by importance (below), traced shadows take the lights that missed out, and contact shadows add
small-scale detail to any light.

## Using it

- **Project Settings > Rendering > Performance > Traced shadow lights** (`TRACED_SHADOW_LIGHTS`, 0-12, default 8): how
  many local lights without a shadow map are traced each frame. 0 turns traced shadows off.
- **Light > Traced Shadows** (point, spot and rect lights; on by default): off lets the light pass through geometry as
  before, for deliberate fill lights.
- **Graphics quality > Traced shadows** (`GraphicsQuality::tracedShadows`, `TracedShadows` in C#): a player ceiling.
  Off on Low, allowed on Medium and above.
- Traced shadows need **shadows enabled**, the **depth prepass** (on by default) and a **DDGI** effect in the scene's
  post-processing: they test against the scene representation DDGI already keeps, so they add no scene of their own.
  With **Ray Source** Auto or Hardware on a ray tracing GPU that is the triangle scene (exact). Otherwise it is the
  voxel scene (coarse, any backend). Without DDGI, lights keep only their contact shadows.

Frame stats report `lightVisibilitySlots`, `tracedShadowLights` and `lightOcclusionSource` ("Hardware", "Voxels" or
empty). The profiler shows **RHI Light visibility** or, when a world test runs, **RHI Light visibility (world
occlusion)**.

## Choosing lights

`LocalLightSelector` (`LocalLightSelection.h`) orders a view's scene lights before the renderer packs them. The renderer
shades at most 16 points, 16 spots and 32 local lights in all, and gives shadow maps to the first casters in list
order. Scene order used to decide both, so the first lights authored won wherever they were.

A light's importance is its brightness at the camera (intensity over squared distance), cut to a twentieth when its range
lies outside the view's side planes. It still lights what the camera sees through GI and reflections, so it stays
eligible, behind every light in view. Each type is ordered as: the casters given shadow maps, the other shaded lights,
then the rest.

- **Hysteresis:** a light that had a shadow map keeps it unless another caster is more than 1.5 times as important
  (`ShadowRetainedBonus`), and a shaded light keeps its place unless beaten by 1.25 times (`RetainedBonus`). This stops
  lights at similar distances swapping as the camera moves.
- **Stable order:** chosen lights keep last frame's relative order, so shadow rows, and with them the cached static
  faces in `PointShadowLayers`, are not reassigned while the chosen set is the same. Lights left out follow in key
  order, so the full list, which GI light injection hashes, changes only when the selection does.

Callers that pass `BasicLighting` directly, such as tests and tools, keep their own order.

## The light visibility mask

Contact and traced shadows share one per-pixel mask (`src/lighting/LightVisibilityMask`, `LightVisibilityCommon.slang`),
which the surface shaders read through the binding contact shadows used before. OpenGL's lit shader already samples 31
of the 32 texture units guaranteed, so a second mask would have taken the last one.

- **Slots:** up to 12 lights (`PLUTO_LIGHT_VISIBILITY_SLOTS`, `LightVisibilityConfig.h`). Each slot is a 6-bit level, and
  four are packed into each of the R, G and B channels of an RGBA32F texture as an exact integer below 2^24. Alpha holds
  the device depth the slots were traced for. Surfaces apply a slot only where their own depth matches it, so draws
  absent from the coverage depth keep their own lighting, as with contact shadows before.
- **Slot policy:** `SelectLightVisibilitySlots` (`LightVisibilitySlots.h`) gives contact slots first: the directional
  light, then up to three lights around the camera. Traced slots follow, for lights marked **Traced Shadows** that have
  no shadow map, brightest at the camera first, up to the budget. A light with both shares one slot. The frame
  parameters carry a light id to slot table (`lightVisibilitySlots`), which `BasicLit.slang` `lightVisibility` looks
  lights up by.
- **One pass:** a compute pass after the depth prepass reconstructs each pixel's position and geometric normal from
  depth. It skips lights out of range and surfaces facing away from a light, which receive none of its light anyway.
  For each remaining slot it multiplies the contact march by the world test. Its shader variant is the world
  representation's.

### World representations

`ILightOcclusionSource` (`LightOcclusionSource.h`) is the seam: a source reports whether it is ready and records the
whole pass with its own shader variant. The mask depends on nothing else, so other representations (signed distance
fields, a dedicated shadow scene) can be added without touching it.

| Source | Shader | Scene | Detail |
| --- | --- | --- | --- |
| `RayQueryLightOcclusionSource` | `LightVisibilityRayQuery.slang` | `RayTracingScene` (DDGI's hardware rays) | Triangles of the traced LODs; leaves stop a ray with their instance's opacity |
| `VoxelLightOcclusionSource` | `LightVisibilityVoxel.slang` | VCT cascades (DDGI's voxel rays) | A voxel (tens of centimetres); opacity above one half blocks, as in DDGI's probe visibility |
| none | `LightVisibility.slang` | | Contact shadows only |

`BasicRenderer::SelectLightOcclusionSource` prefers hardware rays, then voxels, and only uses a scene DDGI traced in
the previous frame. The mask is traced before shading, while DDGI updates after it, so a frame's tests see last frame's
scene: geometry that moved this frame shadows a frame late. The voxel scene must also still be VCT's current atlases,
which are recreated when the voxel resolution changes. The shared scene code lives in `VoxelSceneSampling.slang` with
`VoxelSceneBinding`, and in `RayTracedScene.slang` with `RayTracedSceneBinding`, which DDGI's ray sources use too.

World tests start `normalOffset` (2 cm) off the receiver along its normal and skip the first `traceStart` (5 cm) plus
`traceStartPerDepth` (2 mm per metre of view depth) of the segment. Receivers are reconstructed from depth, and the
traced geometry may be a coarser LOD than the one drawn, so without these a surface would shadow itself. Contact
shadows cover that stretch. Tests stop short of the emitter by its radius, at least 5 cm (a voxel for voxels), so a
lamp's own fixture does not block it. The target point is jittered over the emitter's disc (**Source Radius**; half
the smaller side of a rectangle) per pixel and frame, so TAA resolves a penumbra.

## Limitations

- What DDGI's scene leaves out casts no traced shadow: GPU-skinned characters, transparent geometry, instances below
  **Smallest ray-traced instance** (0.5 m by default) and draws beyond the probe volume's reach. Contact shadows and
  shadow maps still cover them.
- Hardware tests trace each instanced draw's coarsest LOD (**Ray-traced geometry**), so foliage shadows are those of
  its coarsest model.
- Voxel tests are blurry and may darken surfaces within a voxel of a wall on the far side. Voxels outside the cascades
  occlude nothing.
- Traced shadows are opaque-only. Transparent surfaces and glass shade local lights without them, as without contact
  shadows.
- Light reaching a two-sided leaf from behind (diffuse transmission) is not traced, as with contact shadows.

## Validation

- `PlutoGELightVisibilityTests`: slot policy (contact first, budgets, lights with shadow maps or opted out left out,
  shared slots, the 12-slot cap, rectangle extent, id table) and light selection (nearest shaded and shadowed first,
  hysteresis both ways, stable order, out-of-view lights ranked behind, type sequence).
- `PlutoGEVulkanRhiTests --traced-shadows` (hardware rays where available, then voxels) and `PlutoGEOpenGLRhiTests
  --traced-shadows` (voxels): a point light behind a 0.3 m wall must leave the floor behind it at under 35% of the
  light that leaked through. A light with **Traced Shadows** off must keep its light, and open floor must stay within
  5% (no self-shadowing).
- `--contact-shadows` covers contact shadows through the mask on both backends.
- `PlutoGEGraphicsQualityTests` and `PlutoGEGraphicsQualityProjectTests`: the quality ceiling, tier round trips and
  older tier records taking their tier's default.
