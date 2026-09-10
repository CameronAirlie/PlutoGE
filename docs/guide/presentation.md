# Rendering, effects, and audio

[Manual](../README.md) · Previous: [Gameplay](gameplay.md) · Next: [Game systems](game-systems.md)

## Establish a visual baseline

Work from the game camera at the intended output resolution. Start with a simple
opaque material, a directional light, and restrained post-processing. Add one
effect at a time and compare the same view. Rendering paths are evolving across
OpenGL and Vulkan; availability and appearance of advanced effects must be tested
in the actual backend and exported runtime you intend to use.

Keep an inexpensive test room containing a rough wall, a smooth object, glass,
a moving character, and a point light. It makes changes to exposure, shadows,
reflections, and temporal effects easier to diagnose than a fully dressed level.

## Meshes, materials, and shader graphs

MeshComponent supplies visible geometry and its material assignments. Shared
materials affect all users of that asset; create separate material assets when
two props need independent appearance. Per-instance mesh color/emission controls
are available through the managed Mesh wrapper.

In the Material Editor, start with base color and the appropriate surface mode,
then add textures and tune roughness/metallic response where exposed. Check
normal-map appearance under a moving light. A texture that looks correct alone
can still be assigned to the wrong channel or used with unexpected UVs.

**Example: painted metal crate.** Assign a painted base-color texture, keep the
paint predominantly nonmetallic, and use moderate roughness. Compare to an exposed
metal patch under the same environment. Save the material and inspect every
submesh of the imported crate, since one object may use several material slots.

Use a Shader Graph when ordinary material properties cannot express the effect.
Begin with a constant output, save and assign the graph-backed material, and
verify it on a primitive before adding texture samples or procedural operations.
For an animated effect, test during Play as well as in the asset preview. Treat
graph compile errors in the Console as the first diagnostic for a missing effect.

For glass, inspect overlapping surfaces and the background through them. Opacity,
refraction, sorting, and reflections interact, so opaque-material expectations do
not transfer directly. The [glass implementation notes](../rhi-glass.md) describe
the renderer path; verify its current behavior in your selected backend.

## Cameras and composition

CameraComponent controls the game view, including main-camera selection and field
of view. Frame the scene with the authored camera; the editor viewport can hide
clipping, framing, or UI placement problems. Choose clipping distances that cover
your game space without needlessly extending the depth range.

**Example: close third-person camera.** Begin with the Follow rig from the first
game guide. Walk beside a wall and inspect whether the camera clips it. Adjust
collision radius and padding, then test a doorway and a low ceiling. A camera
focus inside solid geometry cannot be fixed just by extending the boom. See
[camera rigs](../CAMERA_RIGS.md) for precise settings and runtime examples.

## Lights and shadows

| Light | Use | First settings to inspect |
| --- | --- | --- |
| Directional | Sun/moon or a broad distant source | Direction, color, intensity, shadow distance and resolution |
| Point | Bulb, torch, localized glow | Position, range, intensity, shadow cost |
| Spot | Flashlight or focused fixture | Direction, cone, range, intensity |

**Example: a corridor.** Establish a dim base environment, add one point light
above a doorway, and verify its useful range. Duplicate only after the single
fixture looks correct. Enable shadows where they contribute readable shape;
many overlapping shadowed local lights can be expensive.

For directional shadows, fit the distance to the playable view, then tune detail
near the camera. Increasing resolution everywhere is not a substitute for useful
coverage. Inspect thin geometry, moving casters, and transitions while moving the
camera. If shadows disappear, check light enablement, caster visibility, shadow
distance, and the selected shadow method before changing material settings.

Advanced shadow paths have separate
[virtual shadow map](../virtual-shadow-maps.md) and
[point shadow filtering](../point-shadow-filtering.md) references. Their engineering
notes include implementation constraints; do not assume every quality option has
the same support in both backends.

## Environment lighting, captures, and baking

Environment maps provide surrounding illumination/reflection context. An IBL
Capture volume gives an area a local captured environment, with Size, Intensity,
Blend Distance, Resolution, and Far Plane controlling its extent and capture.
Baked probe data and other scene bake outputs require a deliberate bake workflow.

**Example: reflective indoor room.** Place a capture inside the room and size its
volume to the intended interior. Start at its default resolution, capture/bake
using the available scene workflow, and inspect a smooth test sphere at the
center and near the boundary. Adjust blending at the doorway. Rebuild capture or
bake data after changing walls or major lighting and save the resulting assets.

Use fast bake quality during layout iteration and increase quality after geometry
and lighting stabilize. Generated lighting data must accompany the scene into
the exported build. If editor lighting looks correct but runtime lighting differs,
check saved bake dependencies and startup scene before retuning lights.

## Post-processing and upscaling

Post-processing transforms the camera image. Save reusable configurations as
`.plutopostprocess` assets where the project workflow exposes presets. Tune in this
order so later effects do not obscure fundamental lighting errors:

1. Exposure and tone mapping: keep ordinary surfaces readable in both bright and
   dark areas; test auto exposure while moving between them.
2. Ambient occlusion and indirect lighting: inspect corners and contact regions;
   excessive occlusion can make every surface look dirty.
3. Reflections: compare smooth and rough surfaces and move objects off screen to
   reveal the limitations of screen-space information.
4. Anti-aliasing/upscaling: inspect moving edges, foliage, particles, and the HUD.
5. Bloom, grading, fog, depth of field, and motion blur: add enough to communicate
   the scene without hiding controls or important targets.

SSAO approximates local occlusion. SSR and SSGI derive information from the camera
view and cannot represent all off-screen geometry. Voxel cone tracing and other
advanced GI paths have their own setup and cost; see [VCT world cache](../vct-world-cache.md).
TAA uses temporal history, while FXAA is an image-space edge filter. Evaluate
temporal artifacts in motion, not only in a screenshot.

**Example: emissive sign.** Make its emissive material visible with bloom disabled,
then enable bloom and tune it until the letters remain legible. If the whole scene
is washed out, correct exposure/material intensity before increasing bloom quality.

[FSR 2](../FSR2.md) and [DLSS](../DLSS.md) have dedicated build/runtime requirements.
Follow those guides before exposing an upscaling option to players; test a fallback
and several output resolutions on the intended hardware.

## Physical sky and volumetric clouds

PhysicalSkyComponent exposes Rayleigh/Mie scattering, ozone, sun and moon colors
and intensity, exposure, and night/star controls. Tune the sky alongside the scene's
directional lighting; changing sky appearance alone is not a complete scripted
day/night gameplay system.

**Example: a clear outdoor level.** Start from the sky defaults and establish a
readable sun direction and exposure. Add clouds only after terrain and character
lighting work. For clouds, place a Volumetric Cloud volume over the visible area,
then tune Coverage and Density before detail erosion or scattering settings.

Cloud Size defines its region. Wind Direction/Speed animate it. Primary Step Count,
Light Step Count, and Render Scale trade sampling quality for GPU cost. Test from
both the ground and any elevated camera positions; a cloud volume far larger than
the actual playable view may cost more without improving the game.

## Oceans and water areas

OceanComponent provides wave and water appearance settings and editable area
polygons. Shallow/deep colors, opacity, smoothness, visibility depth, refraction,
and foam control the surface; underwater fade, turbidity, and light falloff affect
the view below it. Wave amplitude, length, speed, and choppiness shape motion.

**Example: a shallow bay.** Place the water at shoreline height and author its
area/mask to match the bay. Start with modest wave amplitude and adjust shallow
color and visibility depth until the shoreline reads clearly. Move the game camera
across the surface and below it to verify the transition, then add foam.

An animated water surface does not by itself implement swimming, buoyancy,
drowning, or underwater sound gameplay. Implement and test those interactions
separately. Avoid assuming the visible displaced surface is a physics collider.

## Particles

Author a `.plutoparticles` asset in the Particle System Editor and assign it to a
Particle System component. Duration, lifetime, speed, size, emission rate, start
color, gravity modifier, emission shape, and simulation space define the basic
effect. Local space follows the emitter; world space lets emitted particles remain
behind as it moves.

**Example: sparks on demand.** Create a non-looping asset, turn off Play On Awake,
use a short lifetime, and choose a small cone/sphere shape. Attach this complete
class to its emitter to test ten particles with Space:

```csharp
using PlutoGE.ScriptCore;

public sealed class BurstExample : ScriptBehaviour
{
    private ParticleSystemComponent? particles;
    public override void OnCreate() =>
        particles = GameObject.GetComponent<ParticleSystemComponent>();

    public override void OnUpdate(float deltaTime)
    {
        if (deltaTime > 0 && Input.IsKeyPressed(KeyCode.Space))
            particles?.Emit(10);
    }
}
```

For continuous exhaust, enable looping and emission over time, then test world
space while moving the emitter. As an estimate, live particle population grows
with emission rate multiplied by lifetime. Large transparent particles can also
be expensive because they overlap many screen pixels. `Stop(clear: true)` and
`Clear()` help reset test effects; `Pause()` preserves a different playback state.

## Decals and surface effects

Decals project marks such as impacts onto scene geometry. Author a decal material
and a Decal component for a persistent level mark, or use `Decals.Spawn` for a
runtime hit. Size the projection to the intended mark so it does not reach nearby
unrelated surfaces.

**Example: projectile mark.** Raycast the shot, resolve its surface response, and
spawn the returned decal material at the hit with a finite lifetime and fade.
The [surface response example](../SURFACE_RESPONSES.md#runtime-access) shows the
managed call. Check angled surfaces and edges and cap the number of active marks.
See [decals](../decals.md) for projection behavior and current limits.

## Cloth

ClothComponent simulates a subdivided sheet with gravity, damping, constraints,
wind, optional top-edge pinning, and a floor collision option. Width/Height set
size; Segments X/Y control particle density. Solver Iterations and Constraint
Stiffness influence how strongly the sheet maintains its shape.

**Example: a hanging banner.** Start with the default sheet and Pin Top Edge
enabled. Set its material, place it clear of the wall, and test at low wind
amplitude. Increase resolution only if its silhouette needs finer folds. Adjust
floor offset if using floor collision. That setting is not a general promise of
collision with every scene mesh or animated character.

## Audio emitters, listeners, and environments

SoundEmitterComponent plays an assigned clip. Use spatialization for world sounds
and non-spatial playback for music or UI feedback. SoundListenerComponent defines
the listening position; place the intended listener on the player camera or a
deliberate player audio point and avoid ambiguous multiple active listeners.

**Example: a humming machine.** Import a WAV, assign it to a Sound Emitter on the
machine, enable Looping, Spatialized, and Play On Awake, and start with modest
Volume. Walk toward and around it. If it remains inaudible, check clip assignment,
active emitter/listener, playback state, gain, distance, and the audio device.

Use `PlayOneShot()` for repeatable events such as clicks or impacts, `Play()` for
ordinary playback, and `Stop()`/`Pause()` for their respective transport behavior.
Volume and Pitch can be controlled from C#. Match footstep playback to animation
markers and [surface assets](../SURFACE_RESPONSES.md), rather than spawning an
unrelated timer per sound.

Collider Blocks Audio enables participation in obstruction. Test a wall between
listener and source before building complex interiors. Audio Environment Volume
adds box/sphere regions with presets such as Room, Cave, Forest, and Underwater,
plus blend distance and priority.

**Example: a cave entrance.** Size a Cave volume to the interior, leave a blend
region around the entrance, and compare reverb and filtering while walking in and
out. Tune gain after the acoustic effect is clear. Rendering an underwater view
does not automatically author an underwater audio volume.
