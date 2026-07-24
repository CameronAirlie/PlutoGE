# PlutoGE C# Scripting Specification

This document is the source-of-truth guide for humans and AI agents writing C# gameplay scripts for PlutoGE. It describes the public managed API implemented in `engine/scripting/managed/PlutoGE.ScriptCore`.

## Quick start

Project scripts use .NET 8 and normally live in the project's `Assets/Scripts` directory. The editor creates and maintains the project `.csproj`, builds the assembly into `Assets/Managed`, and reloads it after a successful build.

```csharp
using System.Numerics;
using PlutoGE.ScriptCore;

public sealed class MovingPlatform : ScriptBehaviour
{
    [SerializedField] private float speed = 2.0f;
    [SerializedField] private Vector3 direction = Vector3.UnitX;
    [SerializedField] private GameObject? target;

    public override void OnCreate()
    {
        Debug.Log($"Created on {GameObject.Name}");
    }

    public override void OnUpdate(float deltaTime)
    {
        GameObject.Position += direction * speed * deltaTime;

        if (target is not null && Input.IsKeyPressed(KeyCode.Space))
            target.Active = !target.Active;
    }
}
```

Rules:

- Import `PlutoGE.ScriptCore`; import `System.Numerics` for `Vector2`, `Vector3`, and `Quaternion`.
- An attachable script must be a non-abstract class derived from `ScriptBehaviour`.
- The type must be constructible without arguments. A public or non-public parameterless constructor is accepted.
- Use `[SerializedField]` for every field or property that must appear in the editor and persist in scenes/prefabs.
- Use `deltaTime` for frame-rate-independent movement.
- Treat engine references as nullable and check the result of `Find`, `GetComponent`, raycasts, prefab instantiation, and serialized object references.
- Do not call the internal `Native.ScriptBridge` API. Use the public wrappers documented here.

## Lifecycle

Override only the callbacks needed by the script:

```csharp
public override void OnCreate() {}
public override void OnUpdate(float deltaTime) {}
public override void OnLateUpdate(float deltaTime) {}
public override void OnCollisionEnter(GameObject other) {}
public override void OnCollisionExit(GameObject other) {}
```

- Serialized values are applied before `OnCreate`.
- `OnCreate` runs when Play/runtime starts and the attached script instance is started.
- `OnUpdate` runs once per runtime frame.
- `OnLateUpdate` runs after normal script updates.
- Collision callbacks receive the other entity as a `GameObject`.
- There is currently no public `OnDestroy`, fixed-update, trigger-enter, or trigger-exit callback.
- Scripts run only as instances attached through an entity's `ScriptComponent`.

`ScriptBehaviour` provides:

- `public uint EntityId { get; }` — native entity ID.
- `protected GameObject GameObject` — the entity owning this script.
- `protected Vector3 Rotation` — shorthand for the owner's local Euler rotation. Prefer `GameObject` for the full transform API.

## Serialized fields

Supported `[SerializedField]` member types:

| Category | Types |
|---|---|
| Scalars | `bool`, `int`, `float`, `double`, `string` |
| Vectors | `Vector2`, `Vector3` |
| Entity | `uint`, `GameObject` |
| Components | `MeshComponent`, `CameraComponent`, `LightComponent`, `RigidbodyComponent`, `ColliderComponent`, `AnimationComponent`, `CanvasComponent`, `RectTransformComponent`, `UIImageComponent`, `UITextComponent`, `UIButtonComponent`, `ParticleSystemComponent`, `SoundEmitterComponent` |
| Assets | `Prefab`, any concrete class derived from `ScriptableObject` |

Examples:

```csharp
[SerializedField] private float speed = 5.0f;
[SerializedField] private GameObject? target;
[SerializedField] private RigidbodyComponent? body;
[SerializedField] private Prefab? projectile;
[SerializedField] private WeaponSettings? settings;

[SerializedField]
private string DisplayName { get; set; } = "Player";
```

Important serialization behavior:

- The attribute works on instance fields and properties, including private members.
- Static members are ignored.
- A serialized property must have both a getter and setter.
- Unsupported types are ignored by reflection. This includes enums, arrays, lists, dictionaries, `Quaternion`, and arbitrary classes that do not derive from `ScriptableObject`.
- Initializers provide editor defaults because PlutoGE creates a default instance during reflection.
- Component references select an entity containing the matching native component.
- `GameObject` and component references may be `null`.
- A `Prefab` stores an asset reference and offers `Instantiate` methods.
- A `ScriptableObject` reference is loaded from its asset and materialized as the declared derived type.

## GameObject and transforms

The owning object is available through the protected `GameObject` property. Other entities can be found or referenced as `GameObject`.

Properties:

```csharp
uint EntityId
bool IsValid
string Name
Vector3 Position          // local
Vector3 WorldPosition
Vector3 Rotation          // local Euler angles
Vector3 WorldRotation     // world Euler angles
Quaternion RotationQuaternion
Vector3 Scale             // local
Vector3 Forward
Vector3 Right
bool Active               // get/set
string[] Tags
```

Methods:

```csharp
bool HasTag(string tag)
bool HasComponent<T>() where T : class
T? GetComponent<T>() where T : class
bool TryInvoke(string methodName, params object?[] args)
bool Destroy()

static bool Destroy(GameObject? gameObject)
static GameObject? Find(string name)
static GameObject[] FindByTag(string tag)
static GameObject? FindWithTag(string tag)
static GameObject[] FindGameObjectsWithTag(string tag)
```

`GetComponent<T>()` supports every component wrapper listed below. For other class types it searches for another C# script of type `T` attached to the entity.

`TryInvoke` invokes a named method on a script attached to the target entity. Prefer normal typed script references when possible; use this for loose message-style communication. It returns `false` if no compatible target method is found.

PlutoGE's conventional local forward axis is `-Z`; use `GameObject.Forward` instead of hard-coding it when possible.

## Components

All component wrappers inherit `ComponentReference`:

```csharp
uint EntityId
bool IsValid
GameObject GameObject
bool Enabled { get; set; }
```

Components are references to existing native components. The C# API does not currently add or remove native components.

### RigidbodyComponent

Properties: `Mass`, `LinearDrag`, `AngularDrag`, `Friction`, `UseGravity`, `IsKinematic`, `FreezeRotation`, `Velocity`, and `AngularVelocity`.

Methods:

```csharp
void AddForce(Vector3 force)
void AddImpulse(Vector3 impulse)
void AddForceAtPosition(Vector3 force, Vector3 worldPosition)
void AddImpulseAtPosition(Vector3 impulse, Vector3 worldPosition)
```

### ColliderComponent

Properties: `Shape`, `Center`, `Size`, `Radius`, `Height`, `IsTrigger`, and `BlocksAudio`.

`ColliderShape` values: `Box`, `Sphere`, `Capsule`.

### CameraComponent

Properties: `IsMainCamera`, `Fov`.

### LightComponent

Properties: `Intensity`, `Color`.

### MeshComponent

Properties: `Static`, `Color`, `Emission`.

### AnimationComponent

Properties: `ClipCount` (read-only), `ClipIndex`, `Playing`, `Looping`, `Autoplay`, `Speed`, and `Time`.

Methods:

```csharp
string GetClipName(int clipIndex)
float GetClipDuration(int clipIndex)
void Play()
bool Play(string clipName)
void Pause()
void Stop()
void PlayState(string stateName)
void SetBool(string parameterName, bool value)
void SetFloat(string parameterName, float value)
void SetInteger(string parameterName, int value)
void SetTrigger(string parameterName)
void ResetTrigger(string parameterName)
```

### ParticleSystemComponent

Properties: `Playing` and `ParticleCount` (read-only), plus `ParticleSystemAsset`, `Looping`, `PlayOnAwake`, `Duration`, `StartLifetime`, `StartSpeed`, `StartSize`, `GravityModifier`, `EmissionRateOverTime`, `StartColor`, `ShapeSize`, `SimulationSpace`, and `Shape`.

Methods:

```csharp
void Play()
void Pause()
void Stop(bool clear = true)
void Clear()
void Emit(int count = 1)
void Emit(Vector3 worldPosition, int count = 1)
void EmitAt(Vector3 worldPosition, int count = 1)
```

Enums:

- `ParticleSimulationSpace`: `Local`, `World`
- `ParticleShape`: `Point`, `Sphere`, `Box`, `Cone`

### SoundEmitterComponent

Properties: `Playing` (read-only), `Clip`, `Looping`, `Spatialized`, `PlayOnAwake`, `Volume`, and `Pitch`.

Methods:

```csharp
void Play()
void PlayOneShot()
void PlayOneShot(float volumeScale, float pitchScale)
void Pause()
void Stop()
```

### Runtime UI

- `CanvasComponent`: `ScaleFactor`, `SortingOrder`
- `RectTransformComponent`: `AnchoredPosition`, `SizeDelta`, `AnchorPreset`
- `UIImageComponent`: `Color`, `Alpha`, `Texture`, `PreserveAspect`, `FillAmount`
- `UITextComponent`: `Text`, `Color`, `FontSize`
- `UIButtonComponent`: `Interactable`; read-only `IsHovered`, `WasPressed`, `WasReleased`, `WasClicked`

`UIAnchorPreset` values: `TopLeft`, `TopCenter`, `TopRight`, `MiddleLeft`, `MiddleCenter`, `MiddleRight`, `BottomLeft`, `BottomCenter`, `BottomRight`, `Stretch`.

## Input

```csharp
bool Input.IsKeyDown(KeyCode key)          // held
bool Input.IsKeyPressed(KeyCode key)       // went down this frame
bool Input.IsKeyReleased(KeyCode key)      // went up this frame
bool Input.IsMouseButtonDown(MouseButton button)
bool Input.IsMouseButtonPressed(MouseButton button)
bool Input.IsMouseButtonReleased(MouseButton button)

Vector2 Input.MousePosition
Vector2 Input.MouseDelta
Vector2 Input.ScrollDelta
bool Input.QuitRequested
bool Input.CursorLocked { get; set; }
```

`KeyCode` covers letters `A`–`Z`, digits `D0`–`D9`, punctuation, arrows/navigation, modifiers, `Space`, `Escape`, `Enter`, `Tab`, `Backspace`, and `F1`–`F12`.

`MouseButton` values: `Left`, `Right`, `Middle`, `Button4` through `Button8`.

## Physics

```csharp
bool Physics.Raycast(
    Vector3 origin, Vector3 direction, float maxDistance,
    out RaycastHit hit)

bool Physics.Raycast(
    Vector3 origin, Vector3 direction, float maxDistance,
    GameObject? ignoredEntity, out RaycastHit hit)

bool Physics.RaycastTagged(
    Vector3 origin, Vector3 direction, float maxDistance,
    string tag, out RaycastHit hit)

bool Physics.RaycastTagged(
    Vector3 origin, Vector3 direction, float maxDistance,
    string tag, GameObject? ignoredEntity, out RaycastHit hit)

Vector3 Physics.MoveKinematic(
    GameObject gameObject, Vector3 displacement, float skinWidth = 0.02f)
```

`RaycastHit` has read-only `Entity`, `Point`, `Normal`, and `Distance`. Directions are normalized by the wrapper. Zero-length directions and non-positive distances return `false`.

`MoveKinematic` performs collision-aware movement and returns the displacement actually applied. There is also a `uint entityId` overload.

## Prefabs, scenes, and data assets

Prefab use:

```csharp
[SerializedField] private Prefab? enemyPrefab;

var enemy = enemyPrefab?.Instantiate(position, rotation);
var other = Prefab.Instantiate("project://Prefabs/Enemy.plutoprefab");
```

Available overloads accept no transform, a position, or a position and Euler rotation. Instantiation returns `null` on failure.

Scene loading:

```csharp
bool accepted = SceneManager.LoadScene("Game");
bool accepted = SceneManager.LoadScene("Scenes/Game.plutoscene");
bool accepted = SceneManager.LoadScene("project://Scenes/Game.plutoscene");
```

The transition is requested for the end of the current frame.

Reusable data assets derive from `ScriptableObject`:

```csharp
public sealed class WeaponSettings : ScriptableObject
{
    [SerializedField] public float Damage { get; set; } = 10.0f;
    [SerializedField] public float Cooldown { get; set; } = 0.25f;
}
```

`ScriptableObject` provides `AssetReference` and `IsValid`. It is data, not a component, and has no lifecycle callbacks.

## Project files and save data

Use `ProjectStorage` instead of constructing paths from the working directory.
All method paths are relative; rooted paths and `..` paths that escape the
selected storage directory are rejected.

```csharp
// Per-user data: genomes, checkpoints, savegames, and settings.
ProjectStorage.WriteUserDataJson("Evolution/latest-checkpoint.json", checkpoint);
var loaded = ProjectStorage.ReadUserDataJson<EvolutionCheckpoint>(
    "Evolution/latest-checkpoint.json");

// Generated project content during development.
ProjectStorage.WriteAssetText("Generated/best-genome.json", genomeJson);
```

Writes create missing directories and replace the destination atomically.
Absolute roots are available as `Application.AssetsPath` and
`Application.PersistentDataPath`. Prefer user data for runtime-created files;
Assets may be read-only in installed or exported builds.

## Logging

```csharp
Debug.Log("Information");
Debug.LogWarning("Warning");
Debug.LogError("Error");
```

Messages are routed to the PlutoGE script log/editor console.

## Common patterns

### Cache components in OnCreate

```csharp
private RigidbodyComponent? _body;

public override void OnCreate()
{
    _body = GameObject.GetComponent<RigidbodyComponent>();
    if (_body is null)
        Debug.LogError("This script requires a RigidbodyComponent.");
}
```

### Collision handling

```csharp
public override void OnCollisionEnter(GameObject other)
{
    if (other.HasTag("Enemy"))
        other.TryInvoke("TakeDamage", 10);
}
```

### Animation events

Add named markers to a `.plutoclip` in the Animation Clip Editor. Every script
attached to the animated entity can listen for them:

```csharp
public override void OnAnimationEvent(AnimationEvent animationEvent)
{
    if (animationEvent.Name == "Footstep")
        Debug.Log($"Footstep surface: {animationEvent.StringParameter}");
}
```

An event also carries `FloatParameter` and `IntParameter`. Markers fire when
playback crosses their time, including across a looping clip boundary.

### Late-follow camera

```csharp
[SerializedField] private GameObject? target;
[SerializedField] private Vector3 offset = new(0.0f, 3.0f, 6.0f);

public override void OnLateUpdate(float deltaTime)
{
    if (target is not null)
        GameObject.WorldPosition = target.WorldPosition + offset;
}
```

## AI authoring checklist

When generating a PlutoGE script:

1. Use only APIs documented here or verified in `PlutoGE.ScriptCore`; do not assume Unity APIs such as `Transform`, `MonoBehaviour`, `Time`, `Instantiate`, `Destroy`, `GetAxis`, coroutines, or `FixedUpdate` exist.
2. Derive attachable scripts from `ScriptBehaviour`.
3. Add `[SerializedField]` to editor-configurable members and use only supported serialized types.
4. Use nullable references and guard them before access.
5. Cache repeatedly used components in `OnCreate`.
6. Multiply per-second movement and rates by `deltaTime`.
7. Use `System.Numerics` vectors/quaternions, not Unity types.
8. Use `GameObject.Position` for local space and `WorldPosition` for world space deliberately.
9. Avoid modifying a dynamic rigidbody's transform every frame; use forces/impulses, or make it kinematic and use `Physics.MoveKinematic`.
10. Build scripts through the editor after changes and fix all C# build errors before expecting the class in the Script Component picker.

## Implementation references

- Public managed API: `engine/scripting/managed/PlutoGE.ScriptCore/*.cs`
- Example scripts: `engine/scripting/managed/PlutoGE.ScriptCore/Examples`
- Managed/native bridge: `engine/scripting/managed/PlutoGE.ScriptCore/Native/ScriptBridge.cs`
- Runtime host: `engine/scripting/src/HostFxrScriptRuntime.cpp`
- Script component lifecycle: `engine/scene/src/components/ScriptComponent.cpp`
