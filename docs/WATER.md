# Water interaction, splashes, sound and habitats

Water bodies are Ocean components (the sea, and lakes with *Invert Area Mask*) and River components
([Terrain worlds](TERRAIN_WORLDS.md#water), [Ocean waves](OCEAN_WATER_EFFECTS.md)). This page covers what happens
when things touch the water, how it sounds, and how foliage decides whether to grow in it.

## Quick start

1. Select a water entity (Ocean or River) and add **Rendering > Water Effects**.
2. Splashes already use the engine's built-in droplets (`engine://builtin/particles/water-splash`). Optionally pick
   your own particle systems under *Enter Splash*, *Exit Splash* and *Wake Splash* (clear one to skip it), and audio
   clips under *Enter Sound*, *Exit Sound*, *Wake Sound* and *Ambient* (empty sounds are skipped). Ripples need no
   assets.
3. Enter play mode. Dynamic rigidbodies that fall in splash, ring the surface and play the splash sound. Use the
   inspector's **Test Splash** button to tune the profile without throwing anything.
4. For characters (kinematic movement), add **Physics > Water Interactor** so wading leaves a wake. Set *Center
   Offset* to the middle of the body (characters usually pivot at their feet) and *Radius* to about half their height.
5. On foliage types, set **Placement > Water** to *Dry Land Only* or *Underwater Only* (with a depth range). The brush
   and procedural scatter respect it. Procedural rules also gain a **Water Depth** input.

## What touches the water

During play, after each physics update, the water system gives each moving body a sphere:

| Body | Sphere | Velocity |
| --- | --- | --- |
| Dynamic rigidbody | Its collider (sphere radius; capsule half height; box mean half extent; 0.5 m otherwise) | The simulated velocity |
| Entity with a Water Interactor | The interactor's *Radius* and *Center Offset* (0 uses the collider, as above) | How far it moved since last frame, unless it is a dynamic rigidbody |

A **disabled** Water Interactor opts its entity out, rigidbody or not. *Effect Scale* multiplies every response, and
*Ripples*, *Splashes* and *Sounds* turn each kind off for that body (for example, a character whose footstep script
plays its own water steps).

Each body turns into events, using velocity relative to the river's flow:

| Event | When | Intensity |
| --- | --- | --- |
| **Enter** | The sphere's bottom goes under the surface while moving down faster than 0.75 m/s | speed / 6 m/s x sqrt(radius / 0.5 m) |
| **Exit** | It leaves the water moving up faster than 1 m/s | 60% of that, from the upward speed |
| **Wake** | It crosses the surface while moving along it faster than 0.4 m/s, every 0.5 m (or 3/4 of its radius) | half of that, from the speed along the surface |

Intensity is clamped to 0..4, and the *Effect Scale* multiplies it. Bodies first seen in the water, bodies that enter
slowly and bodies that wade out sideways make no splash. A body cannot splash again for 0.2 s, so bobbing does not
chatter. These thresholds are `WaterInteractionSettings` (`scene.GetWaterSystem().SetInteractionSettings`).

## Water Effects profile

| Section | Settings |
| --- | --- |
| **Ripples** | *Strength* (ring height in metres at intensity 1), *Speed* (m/s outward), *Wavelength*, *Lifetime* (s), *Foam* (whitening of fresh rings) |
| **Enter / Exit / Wake Splash** | *Particles*, *Heavy Particles*, *Min/Max Particles* (count at intensity 0 and 1, more above) |
| **Enter / Exit / Wake Sound** | *Sound*, *Heavy Sound*, *Volume* (at intensity 1), *Pitch Variation*, *Min/Max Distance* |
| Thresholds | *Min Splash Intensity* and *Min Sound Intensity* (weaker events make only rings), *Heavy Intensity* (the heavy particles and sound play from here), *Wake Sound Interval* (seconds between one body's wake sounds) |
| **Ambient** | A looping *Sound* heard from the water's nearest point to the listener (on the lake's shore, along the river, straight below over the sea), with its *Volume* and distances. It stops when the listener is out of earshot. |
| **Underwater** | While the listener is under this water: *Low Pass* (0..1, combined with the environment's) and *Gain* |

**Built-in splash.** `engine://builtin/particles/water-splash` is a particle system built into the engine (no file,
like the built-in meshes and materials): up to 600 white droplets thrown 4 m/s up and out of a 35 degree cone, falling
back under full gravity, streaked by their motion and fading within about a second. New profiles use it for all three
splashes; the particle pickers list it with the engine's assets. To change it for a project, create a particle system
asset, start from those values and assign it.

Heavy variants let one profile sound like both a pebble and a person jumping in. Splash sounds are quieter for weaker
events and pitch a little lower for heavier ones. Splash particles come from one hidden world-space emitter per
particle asset (`EmitAt` at the surface), so splashes create no entities after the first. Make splash particle
systems that burst upward (a Cone shape), with gravity and a short lifetime. The emitter only emits when asked to, so
looping and emission rate in the asset do not matter. If an asset fails to load, the inspector lists it during play.

## Ripples

Rings are analytic Gaussian wave packets expanding from each event. They fade with age and as they spread out. Each
body holds up to 32 at once; when that is full, a stronger new ring replaces the faintest one. The ocean renderer adds
their slope to the surface normal (reflections and refraction move) and whitens fresh crests. The detail fades
between 60 and 160 m from the camera. Rivers splash and sound, but their ribbon material does not draw rings.

## Foliage and water

**Foliage habitats.** A foliage type's *Water* setting (`FoliageTypeAsset::waterHabitat`) is *Anywhere* (the
default, so older types grow as before), *Dry Land Only*, or *Underwater Only* with a *Water Depth* range (0 - 0.6 m
for reeds, 1 - 20 m for sea grass). Depth is the still water level minus the ground, so waves never change where
plants grow and generation stays deterministic. The brush skips candidates the habitat rejects. Procedural scatter
skips them too, and warns when an underwater-only type is scattered in a scene without water. Habitats are saved in
scenes, prefabs and `.plutofoliage` files (`WATER_HABITAT` record; older readers skip it).

**Water Depth rule input.** Procedural rule stacks (biome weight, layers, scatter density) can read *Water Depth*:
metres of still water above the ground, negative above the water's level, and -1,000,000 where no water covers the
point. Its default Band (0.25 m .. 1000 m, 0.25 m falloff) means "under water". Invert it for land, or band it
(say -0.5 .. 0.3 m) to paint a shoreline or grow reeds along it. A Water Depth rule *keeps* what its band covers: the
default keeps water, so to keep trees out of the water tick **Invert** (the inspector says what the rule keeps), or
leave the rule out and give the type a *Dry Land Only* habitat. When a scatter rule places nothing, generation warns
which test rejected its candidates. An ocean with no areas is water everywhere, with or without *Invert Area Mask*,
exactly as it is drawn. Generation copies the water bodies on the main
thread (`WaterSurfaceSnapshot`) only when a rule or habitat reads them. Re-generate after moving water.

## Architecture

All of it is in `engine/scene/.../water/`. Each piece is plain data or a small class with one job, and only
`WaterSystem` touches the scene.

| Piece | Role |
| --- | --- |
| `WaterSurfaceSnapshot` | Copies every enabled water body out of a scene. It answers animated or still samples, still depth and nearest surface point. It is thread-safe and shareable, and backs `SampleWaterSurface` and `IsUnderwater`. |
| `WaterHabitat` | Where a plant may grow, given a measured depth. Pure. |
| `WaterRippleField` | One body's live rings: emission, ageing, packing for the GPU, and CPU height and gradient that match the shader (`OceanRipples.slang`). |
| `WaterInteractionTracker` | Turns `WaterProbe`s into Enter, Exit and Wake `WaterInteractionEvent`s. Pure, so it is tested with a synthetic surface. |
| `WaterEffectsProfile` | The tunable response, plus `ChooseSplash` and `ChooseSound`. Plain data, ready to become a shared asset. |
| `IWaterInteractionListener` | The extension point. The built-ins are `WaterRippleResponder`, `WaterSplashResponder` and `WaterSoundResponder`. |
| `WaterSystem` (`Scene::GetWaterSystem()`) | Each frame after physics: snapshot, probes, tracker, then listeners and ring ageing. During the audio update: ambience and the underwater filter. It resets when play starts or stops. |

Add behaviour by registering a listener rather than editing the system. A buoyancy or gameplay response looks like
this:

```cpp
struct SplashDamage final : scene::water::IWaterInteractionListener
{
    void OnWaterInteraction(scene::Scene &scene, const scene::water::WaterInteractionEvent &event) override
    {
        if (event.type == scene::water::WaterInteractionType::Enter && event.intensity > 2.0f)
            /* ... event.entity hit the water hard at event.position ... */;
    }
};
scene.GetWaterSystem().AddListener(std::make_shared<SplashDamage>());
```

`WaterSystem::Disturb(scene, position, intensity)` raises an event without a body (rain, thrown stones, a boat's bow);
every listener responds to it. `Scene::PlayOneShotSound` accepts `looping`, and `Scene::UpdateSoundPlayback` moves a
playing scene sound, which the ambience uses.

## Limits and next steps

- Ripples are a visual normal and foam effect on Ocean surfaces. They do not displace the surface, push bodies, or
  show on river ribbons.
- Bodies are approximated by spheres; long boats should use an interactor per hull section, or `Disturb` along the bow.
- There is no buoyancy yet. It would be another listener, or a system reading the same probes.
- Underwater detection for audio uses the listener's position against the surface under it, not the camera's
  clipping of the surface.
- Managed (C#) bindings for the water system, events and `Disturb` are not written yet.
- The profile lives on the component. Moving it into a shared `.plutowater` asset needs only an asset type around
  `WaterEffectsProfile`.

## Verification

`PlutoGEWaterEffectsTests` covers habitats, snapshots (still and animated samples, depth, nearest shore point), ring
emission, capacity and ageing, and gradients against finite differences. It also covers tracker events (entering,
cooldown, exit, wake spacing, flow-relative velocity, first sight, slow entry, wading ashore), profile choices and
property round trips, habitats in the brush, asset files, component properties and procedural scatter (determinism
included), and the Water Depth rule. A runtime scene drops a rigidbody into the sea and wades an interactor through
it, and checks the events, a gameplay listener, an opted-out body, missing-asset handling, ring data in the ocean's
render packet, fading, and reset on stop.
