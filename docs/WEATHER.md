# Weather

The engine draws weather; a game decides it. Weather is a set of scene-wide components that a game's weather script
changes over time:

| Component | What it does |
| --- | --- |
| `WeatherComponent` | Surface wetness and precipitation intensity (this page). |
| `VolumetricCloudComponent` | Cloud coverage and density: coverage near 1 is an overcast sky ([CLOUD_SHADOWS.md](CLOUD_SHADOWS.md)). |
| `WindComponent` | The wind field foliage bends in, and particles can drift in ([FOLIAGE_WIND.md](FOLIAGE_WIND.md)). |
| `ParticleSystemComponent` | Rain or snow: a world-space emitter the script keeps above the camera. |

Each one follows the first enabled component on an active entity, so a scene has one weather.

## WeatherComponent

Add it from **Environment > Weather**.

- **Wetness** (0 to 1). Rain-soaked surfaces: porous ones (rough and not metallic: soil, bark, rock, wood) darken to
  about half their albedo, and every surface turns glossier. The effect follows how much a surface faces the sky: full
  on ground and roofs, about a third on walls, none on undersides. It applies to opaque lit surfaces (BasicLit,
  terrain layers included), not to glass, water or unlit graphs. A wetness of 0 renders exactly as before and costs
  nothing. Raise it while it rains and let it fall slowly afterwards; surfaces dry over minutes, not seconds.
- **Precipitation** (0 to 1). How hard it is raining. The renderer does not draw it: rain particles, sounds and gameplay
  read it, so they agree on one value.

C#: `WeatherComponent.Wetness` and `Precipitation`.

**Limit:** wetness is not occluded. Ground under a roof or a dense canopy is as wet as open ground. A game that needs dry
interiors can lower wetness while the camera is indoors.

## Rain particles

A particle system asset's **Scene Wind Drag** (per second) makes its particles take up the scene's wind: each step their
horizontal velocity relaxes toward the air's, gusts included, so rain slants and sheets with the gusts the trees show.
0 (the default) ignores the scene's wind. Rain wants about 0.5, snow and leaves about 2. The wind carries particles
sideways only; gravity and the asset's own forces still act.

A rain emitter is a world-space box emitter above the camera. Move it with the camera each frame and scale its emission
rate with `Precipitation`. Collision (Kill) stops drops at roofs, at a raycast per drop, so keep the emitter small and
dense rather than wide.
