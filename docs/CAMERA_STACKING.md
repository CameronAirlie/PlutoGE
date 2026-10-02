# Camera stacking (overlay cameras)

Overlay cameras draw tagged objects on top of the main camera's image. The
usual case is a first-person weapon that should never clip into walls and
can use its own field of view.

## Weapon camera setup

1. Tag the weapon's root entity `Weapon`. Child entities inherit the tag, so
   every mesh in the weapon hierarchy is included.
2. On the main camera, set **IgnoredTags** to `Weapon`. Leave **RenderType**
   as **Base**.
3. Add a second camera as a child of the main camera, so the two cameras
   move together. Set **RenderType** to **Overlay** and **RenderTags** to
   `Weapon`. Set its FOV and near plane for the weapon, for example a
   55 degree FOV and a 0.01 near plane.

The overlay renders only the weapon, with its own depth buffer, and is
composited over the main camera's final image. Runtime UI is drawn on top of
both.

## Settings

| Property | Meaning |
| --- | --- |
| RenderType | **Base** cameras render the scene. **Overlay** cameras are drawn on top of the main base camera. |
| OverlayOrder | Overlays are drawn in ascending order, so higher values appear on top. Overlays with the same value are drawn in hierarchy order. |
| RenderTags | Comma-separated. If set, the camera renders only entities that have one of these tags or have an ancestor with one. |
| IgnoredTags | Comma-separated. The camera never renders entities that have one of these tags or have an ancestor with one. This takes precedence over RenderTags. |
| FilterLightsByTags | Off by default. When on, RenderTags and IgnoredTags also choose which lights illuminate this camera, and only the camera's own tagged geometry casts shadows onto it. |

The base camera is the enabled **Main Camera** with RenderType Base. If no
base camera is marked as main, the first one in the hierarchy is used. Overlay
cameras are never chosen as the base, even if they are marked as main.
Disabled cameras and cameras on inactive entities are skipped. If a scene has
no base camera, its overlays are not rendered. The editor Game view and built
games choose cameras the same way.

A camera's tag filter also applies to its shadow casters. A weapon hidden from
the main camera therefore casts no shadows into the world.

## Lighting and post-processing

By default, overlays use all of the scene's lights and receive shadows from
world geometry, even though that geometry isn't drawn on the overlay. When the
scene uses virtual shadow maps, overlays use cascaded shadows instead. Oceans,
particle systems and the sky are left to the base camera.

To light an overlay separately, turn on **FilterLightsByTags**. The overlay is
then lit only by lights on entities its tags accept, including the sun, and
shadowed only by its own tagged geometry. For example, give a weapon camera
RenderTags `Weapon` and add a light under the weapon (or tag a light
`Weapon`) so the weapon looks the same in a dark corridor or bright sunlight.
World lights and shadows then no longer affect the weapon. Sky lighting still
applies to render-texture cameras, which draw the sky. The same setting works
for render-texture cameras.

Each overlay runs its own post-process chain. An overlay has no background, so
**AutoExposure**, **TAA** and **MotionBlur** are skipped on overlays. Give
overlays a fixed exposure and the same tone mapping and color grading as the
main camera. If the main camera uses auto exposure, the two images can differ
in brightness.

## Limitations

- The overlay covers only pixels where it writes depth: opaque and
  alpha-tested surfaces. Transparent-only surfaces, glass and particles on
  overlays are not composited.
- Overlay edges are not antialiased.
- Camera stacking needs the RHI renderer (Vulkan, or the editor's RHI
  preview). The legacy OpenGL runtime path renders only the base camera and
  ignores tag filters.
- RenderType, OverlayOrder and the tag lists cannot yet be set from C#
  scripts. To show or hide an overlay at runtime, set the overlay camera
  component's `Enabled` property.
