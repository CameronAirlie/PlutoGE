# Gameplay and physics

[Manual](../README.md) · Previous: [World building](world-building.md) · Next: [Presentation](presentation.md)

## Behaviours and serialized settings

Derive gameplay classes from `ScriptBehaviour` and attach them to entities through
Script components. Use `[SerializedField]` for Inspector-authored values, including
references to other entities or prefabs. Serialization applies those values before
`OnCreate`. Ordinary private runtime fields are useful for timers and cached
components, but are not automatically saved as game progress.

Get existing native components with `GameObject.GetComponent<T>()`. This does not
add a component: author required components in the editor or in a prefab. Cache
frequently used references in `OnCreate` and check for null. The current GameObject
and component `IsValid` properties only check for a nonzero entity ID; they do not
prove that a previously referenced entity still exists. Clear owned references
after destruction and reacquire them after scene changes. Use typed script references for direct
gameplay calls; `TryInvoke` is available for loosely coupled named messages.

See [supported serialized types](../CSHARP_SCRIPTING.md) before introducing
custom collections or data structures into Inspector fields. A C# type being
serializable by a general JSON library does not imply the Inspector supports it.

## Choose the right callback

| Callback | Use |
| --- | --- |
| `OnCreate()` | Cache components, initialize runtime state, subscribe to events |
| `OnUpdate(float deltaTime)` | Sample input, advance gameplay timers, update UI |
| `OnFixedUpdate(float fixedDeltaTime)` | Apply forces and other fixed-step physics control |
| `OnLateUpdate(float deltaTime)` | Follow presented character transforms with custom cameras |
| `OnCollisionEnter/Exit(GameObject other)` | React to reported contact begin/end |
| `OnAnimationEvent(AnimationEvent e)` | React to named clip markers |
| `OnDestroy()` | Unsubscribe, dispose networking/document resources, release owned state |

The current scene loop calls fixed scripts before each physics step at `1/60`
second, with at most eight substeps per rendered frame. A render frame can have
zero or multiple fixed updates. Sample one-frame button presses in `OnUpdate`
and consume a queued action once in `OnFixedUpdate`; otherwise a single press
can be missed or applied repeatedly. `OnLateUpdate` follows physics presentation.

Multiply velocities and per-second timers by their callback delta when converting
them into displacement or elapsed amounts. `AddForce` supplies a force to the
physics system; do not multiply that force by delta time again. An impulse is a
one-time change in momentum. PlutoGE uses its own callback names, not Unity's
`Update` or `FixedUpdate`, and supplies no coroutine API.

## Keyboard, mouse, controllers, and action maps

`Input.IsKeyDown` tests a held key; `IsKeyPressed`/`IsKeyReleased` test frame edges.
Mouse buttons have corresponding methods. MousePosition, MouseDelta, and
ScrollDelta provide pointer data; CursorLocked controls pointer capture. Release
capture for clickable menus and restore it when returning to gameplay.

For remappable controls, use `InputActionMap`. This complete example logs a named
action bound to Space and controller A:

```csharp
using PlutoGE.ScriptCore;

public sealed class ActionExample : ScriptBehaviour
{
    private readonly InputActionMap controls = new()
    {
        Actions =
        [
            new InputActionDefinition
            {
                Name = "Interact",
                Bindings =
                [
                    new InputBinding { Kind = InputBindingKind.Key, Key = KeyCode.Space },
                    new InputBinding { Kind = InputBindingKind.GamepadButton,
                                       Button = GamepadButton.A }
                ]
            }
        ]
    };

    public override void OnUpdate(float deltaTime)
    {
        if (deltaTime > 0 && controls.WasPressed("Interact"))
            Debug.Log("Interact requested");
    }
}
```

`GetAxis(name)` sums scaled bindings and clamps the result to `[-1, 1]`; a binding
dead zone suppresses small values. Use negative Scale for the opposite direction
of a keyboard axis. `WasPressed` supports key, mouse-button, and gamepad-button
bindings, not analog threshold crossings. Implement an explicit previous-value
comparison if an analog axis must trigger a one-shot action.

`InputActionMap.Load("project://Input/Controls.plutoinput")` reads a saved mapping.
`CreateVehicleDefaults()` supplies an example vehicle layout. `Save` writes an
asset during development; store player preferences in user data for installed
games. Do not assume `Input.GetAxis` exists: the named-axis method belongs to the
action map.

## Static, dynamic, and kinematic collision

| Object | Setup and movement |
| --- | --- |
| Wall or floor | Collider without a moving Rigidbody; authored transform |
| Falling crate | Collider plus non-kinematic Rigidbody, positive mass, gravity enabled; forces/impulses |
| Scripted character | Collider plus kinematic Rigidbody; collision-aware `Physics.MoveKinematic` |

Use Box, Sphere, or Capsule for simple gameplay shapes. The native Inspector also
supports Terrain and Mesh collision backed by corresponding geometry components;
the managed `ColliderShape` enum currently exposes only the three primitive names.
Do not replace an authored terrain shape with a guessed C# enum value.

Mass, drag, friction, gravity, center of mass, and rotation constraints control a
Rigidbody's response. Keep dimensions and scale valid and match collision to the
visual object. Surface-response assets can override contact friction. Teleporting
a dynamic transform every frame competes with simulation; use physics control
or deliberately choose kinematic movement.

### Example: apply one impulse per press

Attach this complete class to a test crate with a dynamic Rigidbody and Collider.
It is an impulse demonstration, not a grounded jump controller: repeated presses
can lift the crate in midair.

```csharp
using System.Numerics;
using PlutoGE.ScriptCore;

public sealed class ImpulseDemo : ScriptBehaviour
{
    [SerializedField] private float impulse = 5.0f;
    private RigidbodyComponent? body;
    private bool requested;

    public override void OnCreate()
    {
        body = GameObject.GetComponent<RigidbodyComponent>();
        if (body is null) Debug.LogError("ImpulseDemo requires a Rigidbody.");
    }

    public override void OnUpdate(float deltaTime)
    {
        if (deltaTime <= 0) { requested = false; return; }
        requested |= Input.IsKeyPressed(KeyCode.Space);
    }

    public override void OnFixedUpdate(float fixedDeltaTime)
    {
        if (requested && body is not null && body.IsValid)
            body.AddImpulse(Vector3.UnitY * impulse);
        requested = false;
    }
}
```

## Raycasts, contacts, and surface responses

Raycasts return a hit entity, point, normal, and distance. Use the overload that
ignores the querying GameObject for interactions starting inside the player.
`RaycastTagged` filters by tag; check failure before using hit data. A ray is a
line query, so use kinematic movement for moving a volume through a level.

This complete behaviour goes on a camera or other aim entity. Press E while
aiming at collision geometry; it reports the nearest hit within three units.

```csharp
using PlutoGE.ScriptCore;

public sealed class InspectTarget : ScriptBehaviour
{
    public override void OnUpdate(float deltaTime)
    {
        if (deltaTime <= 0 || !Input.IsKeyPressed(KeyCode.E)) return;
        if (Physics.Raycast(GameObject.WorldPosition, GameObject.Forward,
                            3.0f, GameObject, out var hit))
            Debug.Log($"Hit {hit.Entity.Name} at {hit.Distance:0.00} units");
    }
}
```

Ignoring the camera entity does not automatically ignore a separate player
entity; pass the player reference instead when its collider obstructs the ray.
If a ray misses a visible mesh, first check its Collider, enabled state, and shape.
Use [DebugDraw](../EDITOR_WORKFLOWS.md#gameplay-debug-drawing) to visualize rays
during Play. Those overlays are editor diagnostics, not a shipped HUD.

Use collision callbacks for simple contact reactions, guarded by tags or a typed
script lookup. There are no separate public `OnTriggerEnter`/`OnTriggerExit`
callbacks. Do not invent them for a trigger-based pickup. Verify the collision
events produced by your chosen collider/body arrangement in a test scene.

For material-dependent footsteps or impacts, assign a `.plutosurface` to the
Collider and resolve the hit with `SurfaceResponses`. Rendered material color
does not select a contact sound. See [surface responses](../SURFACE_RESPONSES.md)
for dispatching the resolved sound, particles, and decal.

## Spawning, lifetime, and reusable settings

Build an entire spawnable object as a prefab because C# wrappers do not add native
components at runtime. This complete spawner requires a Prefab assignment:

```csharp
using PlutoGE.ScriptCore;

public sealed class SpawnExample : ScriptBehaviour
{
    [SerializedField] private Prefab? objectPrefab;

    public override void OnUpdate(float deltaTime)
    {
        if (deltaTime <= 0 || !Input.IsKeyPressed(KeyCode.P)) return;
        var spawned = objectPrefab?.Instantiate(GameObject.WorldPosition);
        if (spawned is null) Debug.LogWarning("Assign a valid spawn prefab.");
    }
}
```

Instantiate returns the new root or null. Limit population and implement cleanup
in a real game; this demonstration spawns once per press without a cap. Destroy
an owned object with `GameObject.Destroy(objectReference)` and stop using the
reference afterward. `Prefab.Preload(reference)` and `IsReady(reference)` support
preparing immutable prefab data before time-critical spawning; readiness does
not mean that instantiation itself is free.

Use a `ScriptableObject` subclass for shared authored settings such as weapon
damage/cooldown or vehicle tuning. It is an asset, has no behaviour lifecycle,
and is distinct from mutable save-game data. See the
[data asset example](../CSHARP_SCRIPTING.md#prefabs-scenes-and-data-assets).

## Animation clips, graphs, and events

An animated character needs compatible imported skeletal mesh and animation data.
Inspect clip names and durations in the Animation Clip Editor before scripting
transitions. Add Animation to the owning entity and select its clip/graph assets.
First confirm one looping clip plays correctly, then build the graph.

**Example: idle and run.** Create Idle and Run states using the corresponding
clips in an Animation Graph, add a float parameter named `Speed`, and configure
transitions around your chosen movement threshold. A movement controller can
then supply speed using `animation.SetFloat("Speed", velocity.Length())`.
This is a fragment: `animation` is a cached `AnimationComponent` and `velocity`
comes from that controller. Parameter/state names must match the authored graph.

Use `Play("clip name")` for named clip playback and check its boolean result;
`PlayState` selects a graph state. Do not restart the same clip every frame.
`Playing`, `Looping`, `Speed`, and `Time` support direct playback control.

Add named Footstep markers in the Clip Editor. Override `OnAnimationEvent` on a
script attached to the animated entity, check `Name`, and dispatch a sound or
surface response. Markers carry string, float, and integer parameters and fire
when playback crosses their time, including loop boundaries. This keeps sounds
synchronized with animation instead of a separate approximate timer.

For a hand-held prop, author a Skeleton Attachment with a joint name/node target
and adjust the prop's local offset. Test across several poses; a static child
transform alone does not follow a deforming bone. Recheck attachments if a model
reimport changes the skeleton.

## Passive and active ragdolls

Animation owns ragdoll creation and animated/physical pose blending. Active
Ragdoll adds motors and root control that attempt to follow the animation while
remaining physical. Begin with a correctly animated skeletal character, then add
Active Ragdoll through the Inspector's Animation category.

**Example: knockdown.** Enable `RagdollEnabled` on the Animation wrapper and set
`RagdollWeight` to `1` for the physical pose. Disable the Active Ragdoll component
for passive collapse; disabling it does not remove the ragdoll. Apply
`AddRagdollImpulse` for an impact. Disable `RagdollEnabled` to return ownership to
animation. Test recovery and initial overlap separately rather than assuming a
blend value implements a complete stand-up state machine.

See [ragdoll authoring and managed controls](../RAGDOLLS.md) for generated bone
shapes, motor settings, impulses, and current constraints.
