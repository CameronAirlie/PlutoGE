# PlutoGE C# Scripting Specification

This document is the source-of-truth guide for humans and AI agents writing C# gameplay scripts for PlutoGE. It describes the public managed API implemented in `engine/scripting/managed/PlutoGE.ScriptCore`.

For editor setup, complete gameplay examples, and system-by-system workflows,
start with the [game developer manual](README.md). This page is a reference
summary; the public managed source defines the exact API for your checkout.

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
public override void OnFixedUpdate(float fixedDeltaTime) {}
public override void OnLateUpdate(float deltaTime) {}
public override void OnDestroy() {}
public override void OnEnable() {}
public override void OnDisable() {}
public override void OnCollisionEnter(GameObject other) {}
public override void OnCollisionExit(GameObject other) {}
```

- Serialized values are applied before `OnCreate`.
- `Enabled` switches a script off without removing it. A disabled script receives no update, collision or
  animation-event callbacks, but its timers keep running. `OnEnable` runs after `OnCreate` (when the script
  starts enabled) and whenever `Enabled` becomes true. `OnDisable` runs whenever it becomes false, and before
  `OnDestroy` if the script is still enabled. Use it, for example, to stop a dead enemy's AI.
- Scripts start in two phases. When Play starts, and when a prefab is spawned, every script instance is created
  and its serialized fields applied first, and only then does each `OnCreate` run. `OnCreate` can therefore
  safely look up other scripts with `GetComponent<OtherScript>()`, whatever order the entities are in.
- `OnCreate` runs when Play/runtime starts and the attached script instance is started.
- `OnUpdate` runs once per runtime frame.
- `OnLateUpdate` runs after normal script updates, physics presentation, and built-in camera rigs. Camera-relative aiming and reticles can use the final rig pose here.
- `OnDestroy` runs once after a started script is stopped, including when Play
  mode ends or its script component is destroyed. Use it to unsubscribe events
  and dispose resources created by the script.
- Collision callbacks receive the other entity as a `GameObject`.
- `OnFixedUpdate` runs before each fixed physics step, currently at 60 Hz with at
  most eight substeps per render frame. A frame may contain zero or multiple fixed
  callbacks. Capture button edges in `OnUpdate` and consume queued physics actions
  once in `OnFixedUpdate`; see the [impulse example](guide/gameplay.md#example-apply-one-impulse-per-press).
- There are no separate public trigger-enter or trigger-exit callbacks.
- The engine skips `OnUpdate`, `OnFixedUpdate` and `OnLateUpdate` for classes
  that do not override them, so leaving an empty callback out costs nothing per
  frame. Override only the callbacks a script needs.
- Scripts run only as instances attached through an entity's `ScriptComponent`.

### Exceptions

An exception thrown from any callback is caught, so one broken script does not stop the game. It is reported
as an error naming the class, callback and entity, with the full stack trace:

```text
PlayerInput.OnUpdate on 'Player' (entity 4) threw: System.NullReferenceException: Object reference not set...
```

- In the editor the report appears in the **Console**. Select it to see the stack trace; double-click opens
  the first frame in your project's code in the configured script editor (**Preferences > External Tools**).
  **Select Entity** in its details selects the entity.
- Turn on **Error Pause** in the Console toolbar to pause Play when a script throws.
- A script whose `OnCreate` throws is left half-initialised, so it receives no further update, fixed update,
  late update, collision or animation-event callbacks. `OnDestroy` still runs so it can undo what it registered.
- A script that throws every frame keeps running. Its error is reported on the 1st, 10th, 100th, ... occurrence,
  and the Console shows the total count.
- Standalone players and dedicated servers print the same report to the error log.

## Time and timers

`Time` reports game time since Play started:

| Member | Meaning |
|---|---|
| `Time.DeltaTime` | Seconds since the previous frame, scaled by `TimeScale` (what `OnUpdate` receives) |
| `Time.UnscaledDeltaTime` | Real seconds since the previous frame, also while `TimeScale` is 0 |
| `Time.FixedDeltaTime` | Seconds per physics step (what `OnFixedUpdate` receives) |
| `Time.TimeSinceStart`, `Time.UnscaledTimeSinceStart` | Scaled and real seconds since Play started |
| `Time.FrameCount` | Frames since Play started |
| `Time.TimeScale` | Game speed (same as `GamePause.TimeScale`): 1 normal, 0 stopped |

Use unscaled time for anything that must keep moving while the game is paused with `TimeScale = 0`, such as
pause-menu animation. A `Stopwatch` is no longer needed for that.

Timers run code later without hand-written countdowns:

```csharp
public override void OnCreate()
{
    After(3.0f, () => door.Open());              // once, in 3 s of game time
    var beacon = Every(0.5f, ToggleBeaconLight); // every 0.5 s
    After(10.0f, beacon.Cancel);
    After(1.0f, ShowHint, unscaled: true);       // keeps running while paused
}
```

- `After` and `Every` inside a `ScriptBehaviour` are owned by that script and stop when it is destroyed. Outside
  a script, use `Timers.After(seconds, action, owner, unscaled)` and `Timers.Every(...)`;
  `Timers.CancelAll(owner)` stops all of a script's timers.
- Timers fire once per frame before scripts update, in the order they became due. A repeating timer fires at
  most once per frame, so a long frame does not replay missed ticks.
- Every timer stops when Play stops or the scene is replaced.
- A timer that throws is logged and cancelled.

## Multiplayer networking

Dedicated-server scripts can implement `IServerCommandHandler.HandleServerCommand(string)`
to receive local console commands on the simulation thread. Return `false` for
unknown commands and `true` after handling a command (including invalid arguments).
Use `Debug.Log` for responses. The server dispatches to the first started script
that handles the command; `help` is also forwarded so games can list their commands.
Built-in `status` and shutdown commands stay with the native host. This is a local
administration API, not a network RPC. Rebuild ScriptCore and native hosts together
when updating the bridge, and include both in exported server packages.

Import `PlutoGE.ScriptCore.Networking` to use the built-in reliable networking
module. It provides one server accepting multiple clients and a client that
connects to one server. Messages are length-framed TCP packets with a `ushort`
channel and a byte payload. A payload is limited to 1 MiB by default.

Network I/O runs in the background, but all public events are queued. Call
`Poll()` from `OnUpdate` to invoke events safely on the gameplay thread:

```csharp
using PlutoGE.ScriptCore.Networking;

private readonly NetworkServer server = new();

public override void OnCreate()
{
    server.ClientConnected += peerId => Debug.Log($"Peer {peerId} joined");
    server.ClientDisconnected += peerId => Debug.Log($"Peer {peerId} left");
    server.MessageReceived += message =>
    {
        Debug.Log(message.GetString());
        server.Broadcast(message.Channel, message.Payload.Span,
            exceptPeerId: message.PeerId);
    };
    server.Error += exception => Debug.LogError(exception.Message);
    server.Start(7777);
}

public override void OnUpdate(float deltaTime) => server.Poll();
```

Client:

```csharp
private readonly NetworkClient client = new();

public override void OnCreate()
{
    client.Connected += () => client.SendString(1, "hello");
    client.MessageReceived += message => Debug.Log(message.GetString());
    client.Error += exception => Debug.LogError(exception.Message);
    _ = client.ConnectAsync("127.0.0.1", 7777);
}

public override void OnUpdate(float deltaTime) => client.Poll();
```

The channel is an application-defined message type. Reserve constants so both
sides agree, for example `const ushort PlayerMove = 10`. For structured data,
use `SendJson`, `BroadcastJson`, and `message.GetJson<T>()`. Use raw `Send` for
compact binary snapshots.

Key API:

```csharp
server.Start(ushort port, int backlog = 128, string bindAddress = "0.0.0.0");
server.Send(int peerId, ushort channel, ReadOnlySpan<byte> payload);
server.Broadcast(ushort channel, ReadOnlySpan<byte> payload, int exceptPeerId = 0);
await server.StopAsync();

await client.ConnectAsync(string host, ushort port);
client.Send(ushort channel, ReadOnlySpan<byte> payload);
await client.DisconnectAsync();
```

`NetworkServer` and `NetworkClient` implement both `IDisposable` and
`IAsyncDisposable`. Stop or dispose them when leaving a multiplayer session.
TCP is ordered and reliable, which suits connection state, chat, and initial
gameplay replication. High-rate transform snapshots may later use a separate
unreliable transport without changing channel payload formats.

Entity replication is also implemented in this namespace. Use
`EntityReplicationAuthority`/`EntityReplicationReplica` for bounded full
snapshots and interpolation, `ReplicationServerSession`/`ReplicationClientSession`
for session negotiation and late join, and `ReplicatedScene` for prefab bindings.
See [Entity replication](ENTITY_REPLICATION.md) for limits, ownership, lifecycle
and cooperative examples. Prediction, UDP transport and authentication are not
provided by these classes.

`ScriptBehaviour` provides:

- `public uint EntityId { get; }` — native entity ID.
- `protected GameObject GameObject` — the entity owning this script.
- `protected Vector3 Rotation` — shorthand for the owner's local Euler rotation. Prefer `GameObject` for the full transform API.

## Serialized fields

Supported `[SerializedField]` member types:

| Category | Types |
|---|---|
| Scalars | `bool`, `int`, `float`, `double`, `string`, any `enum` |
| Vectors | `Vector2`, `Vector3`, `Vector4`, `Quaternion`, `Color` |
| Entity | `uint`, `GameObject` |
| Components | `MeshComponent`, `CameraComponent`, `LightComponent`, `RigidbodyComponent`, `ColliderComponent`, `AnimationComponent`, `CanvasComponent`, `RectTransformComponent`, `UIImageComponent`, `UITextComponent`, `UIButtonComponent`, `ParticleSystemComponent`, `SoundEmitterComponent` |
| Assets | `Prefab`, `InputActionMap`, `Material`, `Shader`, `Texture`, `Mesh`, `Model`, `AudioClip`, `SceneAsset`, `Animation`, `AnimationClip`, `AnimationGraph`, `ParticleSystem`, `PostProcessPreset`, `RmlDocumentAsset`, `SurfaceResponseAsset`, `LoadingScreen`, any concrete class derived from `ScriptableObject` |
| Asset references | `string` with `[AssetReference(AssetType.X)]`, for any project asset type |
| Arrays | `T[]` or `List<T>` of any scalar, vector, enum or asset type above (not entity or component references) |

Examples:

```csharp
[SerializedField] private float speed = 5.0f;
[SerializedField] private GameObject? target;
[SerializedField] private RigidbodyComponent? body;
[SerializedField] private Prefab? projectile;
[SerializedField] private WeaponSettings? settings;
[SerializedField] private InputActionMap? controls;
[SerializedField] private Material? highlight;
[SerializedField] private Shader? dissolve;
[SerializedField, AssetReference(AssetType.Audio)] private string hitSound = "";
[SerializedField, AssetReference(AssetType.Scene)] private string nextLevel = "";
[SerializedField] private Prefab?[] enemyVariants = [];
[SerializedField] private List<WeaponSettings?> loadouts = [];
[SerializedField, AssetReference(AssetType.Audio)] private string[] footsteps = [];

[SerializedField] private DamageType damageType = DamageType.Fire;
[SerializedField] private Color tint = Color.White;
[SerializedField] private Quaternion spawnFacing = Quaternion.Identity;
[SerializedField] private int[] waveSizes = [3, 5, 8];
[SerializedField] private List<string> barks = ["Over here!", "Found you."];

[SerializedField]
private string DisplayName { get; set; } = "Player";
```

### Inspector attributes

```csharp
[SerializedField, Header("Movement")]
[Tooltip("Metres per second while walking.")]
[Range(0, 10)] public float Speed = 4;

[SerializedField, FormerlySerializedAs("speed")] public float WalkSpeed = 4;
```

- `[Header("Title")]` starts a titled group above the field.
- `[Tooltip("text")]` shows text when the pointer rests on the field.
- `[Range(min, max)]` turns `int`, `float` and `double` fields into sliders.
- `[FormerlySerializedAs("oldName")]` keeps values saved under an earlier name when you rename a field. Scenes,
  prefabs and scriptable assets load the old value; the next save writes the new name. Repeat the attribute
  for several old names.

The Inspector shows field names as readable labels (`walkSpeed` and `m_walkSpeed` both show as "Walk Speed").
Enums are saved by member name, so you can reorder or insert members without breaking saved data. A saved
name the enum no longer has is shown as "(missing)" until you choose a member. `Color` is a linear RGBA value
from 0 to 1; it converts implicitly to and from `Vector4`, and `Color.TryParseHex("#ff8800", out var c)` reads
design-tool hex. `Quaternion` fields are edited as Euler angles in degrees.

Every asset field shows a dropdown of matching project assets in the Inspector (and in the ScriptableObject
editor), accepts drag-and-drop from the Content Browser, and is kept as a cook dependency, so the asset
ships with builds and survives renames. `[MaterialAsset]` and `[InputMappingAsset]` remain as shorthands for
`[AssetReference(AssetType.Material)]` and `[AssetReference(AssetType.InputMapping)]`.

Typed asset handles (`Material`, `Shader`, `Texture`...) derive from `Asset`. They expose `AssetReference`
and convert implicitly to it, so they can be passed to any API that takes a reference string, such as
`Decals.Spawn(hit, highlight, ...)`. `Shader` references shader graph assets.

To add a typed asset of your own, game code included, derive a sealed class and name its asset type:

```csharp
[AssetKind(AssetType.Texture)] public sealed class Decal : Asset;
```

Classes that load their own data (as `InputActionMap` does) are registered in `SerializedAssetTypes` instead,
with their `AssetType`, a load function and a way to read their reference back.

Important serialization behavior:

- The attribute works on instance fields and properties, including private members.
- Static members are ignored.
- A serialized property must have both a getter and setter.
- Members of unsupported types (dictionaries, arrays of entity or component references, and classes that do not derive from `ScriptableObject`) are skipped, and the Console shows a warning naming each one when scripts load.
- Initializers provide editor defaults because PlutoGE creates a default instance during reflection.
- Component references select an entity containing the matching native component.
- `GameObject` and component references may be `null`.
- A `Prefab` stores an asset reference and offers `Instantiate` methods.
- A `ScriptableObject` reference is loaded from its asset and materialized as the declared derived type.
- Arrays appear as reorderable lists (drag the handle to reorder, add and remove elements). Unassigned asset elements are kept as `null` (or `""` for strings), so indices stay stable.
- A scriptable object asset saved before a field existed loads with that field's initializer value; you do not
  need to add the field to the asset file by hand. The ScriptableObject editor shows it and saves it with the
  asset on the next edit.
- Saved numbers use the shortest text that reads back exactly (`17.7`, not `17.700001`).
- An `InputActionMap` is loaded from its `.plutoinput` asset when the script is created; `AssetReference` names its source. A missing or invalid asset logs a warning and leaves the field `null`.

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
static GameObject[] FindWithComponent<T>(bool includeInactive = false) where T : class
static T[] FindComponents<T>(bool includeInactive = false) where T : class
Matrix4x4 WorldMatrix { get; }
```

`FindWithComponent<T>()` and `FindComponents<T>()` walk the active scene in hierarchy order
for a native component wrapper or a script type. Inactive entities and disabled components are
skipped unless `includeInactive` is true. Each call walks the whole scene, so cache the result
on load or when the scene changes rather than calling it every frame. `WorldMatrix` is the full
world transform, including scale, in System.Numerics layout (`Vector3.Transform(point, matrix)`).
`ColliderComponent.TryGetWorldCorners(Span<Vector3>)` and `TryGetWorldBounds` give a collider's
world-space box; spheres and capsules use their bounding box. Together these let scripts build
runtime overviews, such as a minimap from colliders, without an offline bake.

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

`GameObject.IsValid` and `ComponentReference.IsValid` currently test only for a
nonzero entity ID. They are not scene-existence checks; clear references when
their objects are destroyed and reacquire them after scene replacement.

### RigidbodyComponent

Properties: `Mass`, `LinearDrag`, `AngularDrag`, `Friction`, `UseGravity`, `IsKinematic`, `FreezeRotation`, `Velocity`, and `AngularVelocity`.

`CenterOfMass` sets the local center of mass. `GetVelocityAtPoint(Vector3
worldPosition)` returns velocity at a world-space point, including angular motion.

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

These are the managed enum values. The native Inspector additionally supports
Terrain and Mesh shapes; author those with their corresponding source components.

### CameraComponent

Properties: `IsMainCamera`, `Fov`.

### LightComponent

Properties: `Intensity`, `Color`.

### WindComponent

The scene's wind ([foliage wind](FOLIAGE_WIND.md)), reached through the generic property bridge. Foliage types with
wind enabled bend with it every frame, shadows included.

Properties: `Direction` (degrees the wind blows toward: 0 is +X, 90 is +Z), `Speed` (m/s, 0 to 40), `Gustiness`
(0 to 1) and `GustSize` (metres between gust fronts). Values are clamped as in the inspector.

```csharp
public sealed class Gale : ScriptBehaviour
{
    private WindComponent? _wind;
    private float _time;

    public override void OnCreate() => _wind = GameObject.Find("Wind")?.GetComponent<WindComponent>();

    public override void OnUpdate(float deltaTime)
    {
        if (_wind is null) return;
        _time += deltaTime;
        // A storm that builds and fades over a minute.
        var storm = 0.5f - 0.5f * System.MathF.Cos(_time * System.MathF.Tau / 60.0f);
        _wind.Speed = 4.0f + 16.0f * storm;
        _wind.Gustiness = 0.3f + 0.6f * storm;
    }
}
```

### VolumetricCloudComponent

A cloud volume, reached through the generic property bridge (`RegisteredComponent`). Cloud shadows and overcast
light follow its settings every frame ([cloud shadows](CLOUD_SHADOWS.md)), so a weather script can drive the sky
directly.

Properties: `Size`, `Coverage`, `Density`, `Extinction`, `ScatteringAlbedo`, `Anisotropy`, `AmbientLight`,
`CloudColor`, `BaseNoiseScale`, `DetailNoiseScale`, `DetailErosion`, `WindDirection`, `WindSpeed`, `RenderScale`,
`PrimarySteps`, `LightSteps`, `CastShadows`, `ShadowDistance`, `ShadowDensity`. Values are clamped as in the
inspector. `Enabled` is not available for this component.

```csharp
public sealed class Weather : ScriptBehaviour
{
    [SerializedField] public float Storminess; // 0 fair, 1 overcast
    private VolumetricCloudComponent? _clouds;

    public override void OnCreate() => _clouds = GameObject.Find("Clouds")?.GetComponent<VolumetricCloudComponent>();

    public override void OnUpdate(float deltaTime)
    {
        if (_clouds is null) return;
        _clouds.Coverage = 0.35f + 0.65f * Storminess;
        _clouds.Density = 0.4f + 0.4f * Storminess;
        _clouds.WindSpeed = 6.0f + 10.0f * Storminess;
    }
}
```

### MeshComponent

Properties: `Static`, `Color`, `Emission`.

`SetGeometry(positions, indices, normals = default, uvs = default)` replaces the drawn mesh with a script-built
triangle list in the entity's local space, for geometry rebuilt at runtime (vision cones, trails, area markers).
The component keeps its materials. The first call creates a mesh owned by the component; later calls replace its
contents in place (renderers re-upload through the content revision), so rebuilding several times a second is
fine. Normals and uvs are optional; missing normals are smoothed from the triangles and tangents are always
generated. An empty triangle list draws nothing. `ClearGeometry()` restores the mesh the component had before.
The geometry is not saved with the scene. Spans let you pass pooled lists without copying:

```csharp
var mesh = marker.GetComponent<MeshComponent>()!;
mesh.SetGeometry(CollectionsMarshal.AsSpan(positions), CollectionsMarshal.AsSpan(indices),
                 uvs: CollectionsMarshal.AsSpan(uvs));
```

`SetGeometry` returns false for invalid input (an index past the vertices, a partial triangle, non-finite
positions, normal/uv counts that differ from the positions) and on hosts built before the API;
`MeshComponent.SupportsGeometry` reports which. Native: `MeshComponent::SetRuntimeGeometry` and
`render::Mesh::ReplaceGeometry` (tested by `PlutoGEMeshRuntimeGeometryTests`).

### AnimationComponent

Properties: `ClipCount` (read-only), `ClipIndex`, `Playing`, `Looping`, `Autoplay`, `Speed`, and `Time`.

Ragdoll controls also include `RagdollEnabled`, `RagdollWeight`,
`AddRagdollImpulse(Vector3 impulse)`, and `ResetRagdoll()`. See
[ragdolls](RAGDOLLS.md) for setup and the `ActiveRagdollComponent` wrapper.

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

Spatialized range: `MinDistance` (full volume within it), `MaxDistance` (silent beyond it) and
`Rolloff` (falloff steepness in between). Set them before `PlayOneShot` to give one shared
emitter different ranges per sound, for example loud and suppressed gunfire.

Methods:

```csharp
void Play()
void PlayOneShot()
void PlayOneShot(float volumeScale, float pitchScale)
void Pause()
void Stop()
```

### TerrainComponent

The ground under a point, and its [material layers](TERRAIN_LAYERS.md) (dust, grass, rock...): use it to pick
footstep sounds or effects by surface, keep AI on roads, or paint tyre tracks and scorch marks during play.
Runtime painting lasts for the play session and is not saved. Positions are world space; layer indices follow
the inspector's order.

Properties: `LayerCount`.

Methods:

```csharp
string GetLayerName(int layer)
int FindLayer(string name)                          // case-insensitive; -1 when missing
bool TryGetSurface(Vector3 worldPosition, out float height, out Vector3 normal)
float[] GetLayerWeights(Vector3 worldPosition)      // per layer, summing to 1; empty off the terrain
int GetDominantLayer(Vector3 worldPosition)         // -1 off the terrain or without layers
bool PaintLayer(Vector3 worldPosition, int layer, float radius, float opacity = 1, float hardness = 0.5f)
```

```csharp
var terrain = ground.GetComponent<TerrainComponent>();
var surface = terrain.GetLayerName(terrain.GetDominantLayer(transform.Position)); // "Gravel" -> gravel footsteps
terrain.PaintLayer(wheelPosition, terrain.FindLayer("Road"), radius: 0.4f, opacity: 0.3f);
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

Gamepad input is also available through `Input` using `GamepadButton` and
`GamepadAxis`. `InputActionMap` maps named actions to keyboard, mouse, and
controller bindings, with `GetAxis`, `IsDown`, and `WasPressed` queries. A binding's optional
`Trigger` (`Tap`, `Hold`, `MultiTap`) lets actions share a control; maps that use them need
`Update(deltaTime)` once per frame. See
[input examples and edge semantics](guide/gameplay.md#keyboard-mouse-controllers-and-action-maps).

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

## Navigation

Navigation queries use an entity containing a `NavigationMeshComponent`. Passing the navigation mesh explicitly supports scenes containing more than one navigation mesh.

```csharp
bool Navigation.ProjectPoint(
    GameObject navigationMesh, Vector3 point, out Vector3 projected,
    float agentRadius = 0.0f, float agentHeight = 0.0f)

NavigationPath Navigation.FindPath(
    GameObject navigationMesh, Vector3 start, Vector3 end,
    float agentRadius = 0.0f, float agentHeight = 0.0f)
```

`ProjectPoint` finds the nearest walkable point (in three dimensions, so a point on an upper floor projects onto that floor rather than the one beneath it) that accommodates the requested agent dimensions. It returns `false` when the object is invalid, does not contain a navigation mesh, the mesh is unavailable, or no suitable point exists.

`FindPath` returns a `NavigationPath` with read-only `Points` and `Complete` properties. `Points` contains world-space waypoints, including the requested start and end positions when a route is found. `Complete` is `false` and `Points` is empty when no route can be found or the navigation mesh is unavailable. Negative agent radius and height values are clamped to zero.

```csharp
[SerializedField] private GameObject? navigationMesh;
[SerializedField] private GameObject? target;
[SerializedField] private float agentRadius = 0.4f;
[SerializedField] private float agentHeight = 1.8f;

public override void OnUpdate(float deltaTime)
{
    if (navigationMesh is null || target is null)
        return;

    var path = Navigation.FindPath(
        navigationMesh,
        GameObject.WorldPosition,
        target.WorldPosition,
        agentRadius,
        agentHeight);

    if (path.Complete && path.Points.Count > 1)
    {
        Vector3 nextWaypoint = path.Points[1];
        // Move toward nextWaypoint.
    }
}
```

## Prefabs, scenes, and data assets

Prefab use:

```csharp
[SerializedField] private Prefab? enemyPrefab;

var enemy = enemyPrefab?.Instantiate(position, rotation);
var other = Prefab.Instantiate("project://Prefabs/Enemy.plutoprefab");
```

Available overloads accept no transform, a position, a position and Euler rotation, or a position, `Quaternion`
rotation and optional parent. Instantiation returns `null` on failure.

The new object is placed and parented before any of its scripts run `OnCreate`, so `OnCreate` sees the final
transform. To hand data to a spawned script, use `Instantiate<T>` with a `configure` callback. It runs after
the object's scripts are created and their serialized fields applied, but before any `OnCreate`:

```csharp
[SerializedField] private Prefab? bulletPrefab;

var bullet = bulletPrefab?.Instantiate<Bullet>(muzzle.Position, muzzle.RotationQuaternion, b =>
{
    b.Damage = damage;
    b.Owner = GameObject;
});
```

`T` can be a script or an engine component on the prefab's root; the call returns that component, or `null`
(with a warning) when the root has none. There is no need to pass data through static "pending" dictionaries
keyed by entity ID.

`Prefab.Preload(reference)` parses and caches a prefab ahead of its first instantiation (for example in a
spawner's `OnCreate`, while the scene loads); `Prefab.IsReady(reference)` reports whether the cached copy is
current.

Scene loading:

```csharp
bool accepted = SceneManager.LoadScene("Game");
bool accepted = SceneManager.LoadScene("Scenes/Game.plutoscene");
bool accepted = SceneManager.LoadScene("project://Scenes/Game.plutoscene");
```

The transition is requested for the end of the current frame.

For additive loading, `SceneManager.LoadAdditive(string sceneAssetReference,
bool activateWhenReady = true)` returns a nullable `SceneSection`. Its `Status`
contains `State`, `Progress` and `Error`; `Activate`, `Cancel`, `Unload` and
`Forget` return whether the requested operation was accepted. `Forget` releases
a completed request; unload an active section first. Reads run asynchronously,
while parsing and activation run on the scene thread. Handles are scoped to
`SceneManager.RuntimeGeneration` and become stale after scene replacement.
See [Scene streaming](SCENE_STREAMING.md) for reference remapping, ownership,
navigation and cooked content constraints.

Reusable data assets derive from `ScriptableObject`:

```csharp
public sealed class WeaponSettings : ScriptableObject
{
    [SerializedField] public float Damage { get; set; } = 10.0f;
    [SerializedField] public float Cooldown { get; set; } = 0.25f;
}
```

`ScriptableObject` provides `AssetReference` and `IsValid`. It is data, not a component, and has no lifecycle callbacks.

`ScriptableObject.Load<T>(assetReference)` materializes a data asset at runtime, for example to
restore an item definition recorded in a save file. It returns null for empty or missing
references and for assets of a different type; each call creates a new instance, so cache the result.

Scriptable objects held in serialized fields follow edits to their asset while the game runs: before a
script's update its scriptable fields are polled about every quarter second. A loose `project://` asset is
only re-read when its file's write time changes; other references are re-read on each poll, and assets with
no loose file (packaged content) every few seconds.

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

Messages are routed to the PlutoGE script log/editor console, labelled **Script**.

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

1. Use only APIs documented here or verified in `PlutoGE.ScriptCore`; do not assume Unity APIs such as `Transform`, `MonoBehaviour`, coroutines, or unqualified `Instantiate`/`Destroy` exist. Use `Time` and `After`/`Every` timers for timing. Use `OnFixedUpdate` for fixed-step callbacks and `InputActionMap.GetAxis` for named input axes.
2. Derive attachable scripts from `ScriptBehaviour`.
3. Add `[SerializedField]` to editor-configurable members and use only supported serialized types.
4. Use nullable references and guard them before access.
5. Cache repeatedly used components in `OnCreate`.
6. Multiply per-second movement and rates by `deltaTime`.
7. Use `System.Numerics` vectors/quaternions, not Unity types.
8. Use `GameObject.Position` for local space and `WorldPosition` for world space deliberately.
9. Avoid modifying a dynamic rigidbody's transform every frame; use forces/impulses, or make it kinematic and use `Physics.MoveKinematic`.
10. Pass the intended navigation-mesh entity to `Navigation.ProjectPoint` and `Navigation.FindPath`; do not assume a global navigation mesh.
11. Build scripts through the editor after changes and fix all C# build errors before expecting the class in the Script Component picker.

## Implementation references

- Public managed API: `engine/scripting/managed/PlutoGE.ScriptCore/*.cs`
- Example scripts: `engine/scripting/managed/PlutoGE.ScriptCore/Examples`
- Managed/native bridge: `engine/scripting/managed/PlutoGE.ScriptCore/Native/ScriptBridge.cs`
- Runtime host: `engine/scripting/src/HostFxrScriptRuntime.cpp`
- Script component lifecycle: `engine/scene/src/components/ScriptComponent.cpp`

Pointer-ray queries in hosted game viewports sample the current OS cursor using the viewport's host-window logical bounds. Call `CameraComponent.TryGetPointerRay` during `OnLateUpdate` for camera-relative aiming. Runtime UI event coordinates remain frame snapshots so UI processing is deterministic. A rendered world reticle still incurs normal rendering/presentation latency.

Dedicated servers accept `--mode <name>` and `--map <name>` as application-defined
startup settings (`PLUTOGE_SERVER_MODE` / `PLUTOGE_SERVER_MAP`). Game scripts own
mode/map validation and defaults. Headless hosts now honor `SceneManager.LoadScene`
requests at the end of a simulation tick, constructing the next scene before
stopping the old runtime and starting the new one. A failed construction leaves
the current scene running. Scene changes reset simulation components and network
sessions; games must arrange reconnection where needed.
