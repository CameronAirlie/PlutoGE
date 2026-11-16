# Airborne particles

Dust motes, pollen, spores, ash and light snow are specks in the air everywhere around the player. They are ordinary
particle systems with a few settings that make them work as an environment effect rather than an emitter:

| Setting | Where | What it does |
| --- | --- | --- |
| Simulation Space: **Camera Volume** | Shape | Fills a box that every camera carries with it, so the field never runs out. |
| Lighting: **Scattering** | Lighting | Lights specks as the volumetric fog is lit: glowing in sunbeams, gone in shade. |
| **Near Fade Distance**, **Min Screen Size** | Rendering | Keep specks from blooming at the lens or flickering far away. |
| **Prewarm** | Top | Starts a looping system full instead of filling up over a lifetime. |
| **Scene Wind Scale** | Motion | Drifts with the scene's wind and gusts at a fraction of its speed. |
| **Wet Weather Fade** | Rendering | Thins the specks in rain and on wet ground. |

All of them are particle asset settings (`.plutoparticles`), edited in the Particle System editor. Any combination
works on any system: a smoke plume can use Near Fade, a rain field can be a Camera Volume.

## Camera Volume

A Camera Volume system spawns its particles evenly through a box (**Volume Size**, world metres) around the emitter.
The particles then move in world space under their usual forces. Each view draws them wrapped into a box of the same
size centred on its camera: space is tiled with the box, and the view shows the one copy of each particle that lies
within half a box of it.

- The field surrounds every camera, the editor's included, wherever the emitter stands.
- A particle inside the box does not move when the camera moves. One that leaves it on one side reappears on the
  other, so walking through the field never empties it.
- **Volume Edge Fade** fades particles out over that share of the half-size toward each face, so wrapping is never
  seen. 0.3 to 0.5 suits motes.
- The emission shape, and the emitter's rotation and scale, are ignored: the volume is axis aligned.
- Density is emission rate x lifetime / volume. Only specks within about 12 m are big enough to see, so a small box
  (24 x 10 x 24 m with 4000 specks: one per 1.4 m³) looks denser than a large one with the same count.

Collisions, trails and sub-emitters run on the simulated (unwrapped) positions, so leave them off for camera volumes.

## Scattering lighting

**Lighting** picks how light reaches a particle: Unlit (the authored colour), Smoke (lit puffs, with up to four local
lights) or Scattering. Scattering treats each speck as a bit of the fog's medium (`Particles.slang`
`scatteringLighting`):

- **Sun:** the directional light through a Henyey-Greenstein phase function (**Anisotropy**, -0.9 to 0.9), scaled so
  even scattering is 1. Dust and pollen scatter forward (about 0.6): looking toward the sun they shine, with the sun
  behind you they all but vanish. **Sun Strength** scales it.
- **Shadows** (**Sun Shadows**): the sun's virtual shadow maps or cascades, the cloud shadow map, and the terrain sun
  occlusion beyond the shadow maps. This is the same visibility the volumetric fog uses
  (`DirectionalVolumeShadow.slang`), so specks light up exactly inside the fog's light shafts.
- **Sky:** the sky's light under the cloud cover, scaled by **Sky Strength**.

The particle's colour is its albedo; its alpha its coverage. Shadows are sampled per pixel of the billboard, so large
puffs show shafts across them too. Shadowed light is the only extra cost, and only for scattering systems.

## Fitting the view

- **Near Fade Distance** (m): particles nearer than this fade, gone at half of it. Specks drifting through the
  camera's near plane otherwise become large blurry discs.
- **Min Screen Size** (pixels): a particle narrower on screen is drawn this wide with its opacity lowered by the area
  it gained, so it keeps the light it had. Sub-pixel billboards otherwise sparkle on and off as they cross pixel
  centres. 1 to 2 pixels suits motes.

Both are worked out per view on the CPU (`src/ParticleViewShaping.h`), with the camera volume's wrapping and edge fade.

## Weather and wind

- **Wet Weather Fade** (0 to 1) multiplies opacity by `1 - fade x max(wetness, precipitation)` from the scene's
  `WeatherComponent` ([WEATHER.md](WEATHER.md)): rain scavenges dust from the air and damp ground raises none. Scenes
  without weather are dry.
- **Scene Wind Scale** scales the scene wind velocity that **Scene Wind Drag** relaxes particles toward. Motes in
  sheltered air drift at a fraction of the wind (0.1 to 0.2) while still following its direction and gusts, so they
  move with the grass and the trees.
- **Turbulence** adds the slow swirl; a tiny Gravity Modifier lets specks settle.

## Prewarm

A looping system with **Prewarm** starts as if it had played for one particle lifetime (lifetime x (1 + variation)),
simulated in steps of up to 0.2 s when it starts playing. A camera volume field is then full on the first frame. It
also applies after an asset is saved while the system plays, so tuning in the editor shows the full field at once.
Prewarming costs one CPU burst at start, proportional to particles x lifetime / 0.2 s.

## A dust recipe

The SurvivalGame sample's `AirDust` effect (its `Tools/levels/atmosphere.py`):

| Setting | Value |
| --- | --- |
| Camera Volume | 24 x 10 x 24 m, edge fade 0.5 |
| Emission, lifetime | 300 /s, 14 s (variation 0.3), max 6000, prewarm |
| Size | 16 mm (variation 0.5), min screen size 1.5 px, near fade 1.5 m |
| Colour | 0.95, 0.88, 0.74, alpha 0.8, held by Colour over Lifetime; fade in 0.2, out 0.25 |
| Motion | speed 0.05, gravity 0.002, drag 0.3, turbulence 0.22 at 0.35, wind drag 0.5 at scale 0.15 |
| Scattering | anisotropy 0.5, sun 1.0, sky 1.5, sun shadows (sun intensity 15) |
| Wet Weather Fade | 0.85 |

Sun Strength depends on the scene's sun intensity: tune it on the scene's exposure until specks looking toward the sun
glint without blooming.

## Code

| Piece | Role |
| --- | --- |
| `assets/ParticleSystemAsset.h` | `ParticleSimulationSpace::CameraVolume`, `ParticleLighting`, the scattering, camera volume and view fade settings, and their `Sanitized` ranges. |
| `scene/ParticleSystemComponent` | Spawning through the volume, prewarm, wind scale. |
| `render/src/ParticleViewShaping.h` | Wrapping, edge and near fades, minimum screen size, weather fade (pure; `PlutoGEAirborneParticleTests`). |
| `RhiSceneRenderer` | Applies them per view while collecting particle draws; fills the scattering lane (28). |
| `BasicRenderer` | Binds the sun's shadow resources to the particle pipelines (`BindSunShadowResources`, `BindSunOcclusionMaps`, shared with fog and water); fills the terrain shadow lane (29). |
| `shaders/DirectionalVolumeShadow.slang` | Sun visibility in the air, shared by volumetric fog and particles. |
| `shaders/Particles.slang` | `scatteringLighting`. |

Tests: `PlutoGEAirborneParticleTests` (CPU: wrapping, fades, screen fit, asset round-trip and legacy smoke files,
camera volume spawning, prewarm, wind scale) and `PlutoGE{Vulkan,OpenGL}ParticleScatteringTests`
(`--particle-scattering`, `tests/ParticleScatteringRenderingChecks.h`: a speck under a roof against one in the sun, Sun
Shadows off, forward against backward scattering).

## Limits

- A camera volume is one box tiled through space, so its pattern repeats every Volume Size; at the default sizes the
  repeat lies beyond the edge fade and is never seen in one view.
- Scattering specks are not lit by local lights; use Smoke for torch-lit dust.
- Specks inside caves are lit by the sky as outside, as the volumetric fog is not (it reads the baked ambient volumes'
  sky visibility). Disable the effect indoors or lower its weather fade there.
- Simulation is on the CPU, like every particle system: a few thousand specks cost well under a millisecond.
