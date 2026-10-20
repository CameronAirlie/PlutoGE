# Night sky rendering

This page describes how the physical sky renders night, and where to extend
it. For authoring settings, see the
[presentation guide](guide/presentation.md#physical-sky-and-volumetric-clouds).

## Approach

Other engines approach night in a few common ways:

- **Unreal Engine (SkyAtmosphere)** scatters up to two atmosphere lights,
  with index 0 for the sun and index 1 for the moon. Each is an ordinary
  directional light. Stars come from a separate sky-sphere material.
- **Unity HDRP (Physically Based Sky)** treats the moon as a celestial-body
  directional light with a phase, and adds a rotatable space emission
  layer for stars.
- **Jensen et al., "A Physically-Based Night Sky Model" (SIGGRAPH 2001)**
  models moonlight as sunlight reflected by the lunar surface. Its
  brightness depends on the phase angle and falls off much faster than the
  lit fraction. It also models starlight, the Milky Way, airglow and
  zodiacal light.
- **Games with a single shadowed directional light** usually hand that
  light from the sun to the moon at night.

PlutoGE combines these. The atmosphere is lit by both the sun and the moon,
as in Unreal. The moon is phase-shaded from the actual sun direction, and
its brightness follows the lunar phase law, as in Jensen et al. The single
shadowed directional light switches to the moon, as in many games. Stars and
the Milky Way live in a celestial frame that turns with the sun.

## Architecture

| Piece | Responsibility |
| --- | --- |
| `render::PhysicalSkySettings` (`PhysicalSkyModel.h`) | Plain authoring data. `PhysicalSkyComponent` only owns and serializes it. |
| `ResolveCelestialState` | Derives moon direction, phase, brightness and the world-to-star rotation from the sun direction. A future time-of-day or astronomical driver replaces only this step. |
| `ResolveCelestialLight` | Chooses sun or moon for the scene's directional light. The handoff happens while both are dark, so it never pops. |
| `PackPhysicalSkyParameters` | Packs the GPU lanes defined in `PhysicalSkyLayout.h`. |
| `PhysicalSkyModel.slang` | Atmosphere: sun and moon single scattering, twilight, airglow, light pollution, ground. Shared by the sky pass and every lit surface. |
| `PhysicalSkyCelestial.slang` | Moon disc, stars and Milky Way. Procedural detail sits behind `physicalSkyMoonAlbedo` and `physicalSkyStars`. |
| `EvaluatePhysicalSkyEnvironment` | CPU mirror of the atmosphere for probe baking. Keep it in sync with the Slang model. |

`PhysicalSkyLayout.h` is shared by C++ and Slang. It defines the lane
indices and twilight thresholds, so they cannot drift between the sky,
lighting and baking. The sky packet's `worldToLocal` carries the star-frame
rotation.

`BuildSceneLighting` records the true sun in `BasicLighting::sunDirection`
and sets `directionalIsMoon` when the moon has the light. Atmosphere passes
(the sky and clouds) use that sun, not the directional light.

## Behaviour notes

- Lighting integrals (ambient, specular convolution, fog, baking) exclude
  the sun and moon discs, because the directional light owns them. They
  also exclude stars, which contribute negligible light.
- Stars sit on a cube-face lattice in the celestial frame, so they do not
  pinch at the poles. Each is sized to the pixel footprint with constant
  flux. Very wide fields of view at low resolution under-sample them.
- Twinkle uses the post-process time, which wraps about every 68 seconds.
- The legacy OpenGL render-pass pipeline places the moon with this model,
  but keeps its simpler look and does not light the scene with the moon.

## Extension points

- **Time of day:** drive the directional light, and set `moonPhase` and
  `starRotation`, from a clock and date. Real astronomical positions can
  replace `ResolveCelestialState` without touching shaders.
- **Authored assets:** a moon albedo texture, a star catalogue or a
  Milky Way cubemap can replace the procedural functions in
  `PhysicalSkyCelestial.slang`.
- **Night perception:** a scotopic (Purkinje) tone-mapping stage, which
  shifts colour toward blue and reduces saturation and detail in low light,
  pairs naturally with auto-exposure limits for night scenes.

Tests: `PlutoGEPhysicalSkyModelTests` (celestial model, light handoff,
scene integration, serialization) and `PlutoGEVulkanNightSkyTests` /
`PlutoGEOpenGLNightSkyTests` (rendered moon phase, stars, moonlit sky and
daytime invariance).
