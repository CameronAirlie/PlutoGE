# Camera rigs (M08)

Add **Camera Rig** to an entity that already has a **Camera** component. Make that
camera the main camera and assign **Target Entity** in the inspector. Play the
scene to see the rig; editing a scene does not move the camera. Adding/removing the
component and editing its settings use the existing scene undo/redo history.

## Settings

| Setting | Behavior |
| --- | --- |
| Target Entity | Entity ID resolved in the owning scene each tick; prefab copies remap internal targets. |
| Mode | Follow uses Offset; Orbit uses Yaw, Pitch, and Distance. |
| Focus Offset | World-space offset from the target origin to the look-at point. |
| Offset | Camera offset from the focus point in Follow mode. |
| Follow Heading | Rotate the Follow offset by the target's rotation, ignoring its scale. |
| Yaw / Pitch | Orbit angles in degrees; yaw zero puts the camera on the positive Z side of the target. Pitch is limited to ±89°. |
| Distance | Orbit boom length. |
| Smoothing Seconds | Exponential position/focus smoothing time constant; zero snaps. |
| Collision Radius | Sphere swept from focus to the final camera position; zero disables avoidance. |
| Collision Padding | Extra clearance before the sweep contact. |

The rig looks along the camera's negative Z axis. It runs after physics presentation
and script LateUpdate, before audio listener sampling. Use one camera driver per
camera so scripts and rigs do not compete to set its transform. Ordinary rotated
parents are supported; unparented or uniformly scaled camera parents give the most
predictable orientation. A singular parent freezes the rig rather than generating
an invalid transform.

Collision uses the scene's physics query world and a sphere sweep, including the
final shake and blend displacement. It ignores triggers and both the camera and
target hierarchies. Choose a radius large enough to protect the camera's near
plane. The focus should start outside obstacle geometry; this is boom obstruction
avoidance, not a solver for escaping an embedded target. Occlusion shortens the
boom immediately. Multiple rigs can run, but only the selected main camera renders.

## C# control

`CameraRig` controls an existing authored rig and returns `false` when the runtime
or rig is unavailable. `Configure` changes serializable settings; `Orbit` changes
angles and distance without resetting smoothing. A null Configure target clears
following. `BlendTo` transitions from the current camera pose to another target
using the same rig settings and a smoothstep curve. It can interrupt a previous
blend. `Shake` starts/replaces a bounded, deterministic, fading positional shake;
shake does not accumulate into the smoothed base pose. `Snap` clears transient
state and snaps on the next simulation tick.

```csharp
CameraRig.Configure(cameraObject, characterObject, new CameraRigSettings {
    Mode = CameraRigMode.Orbit,
    Distance = 4,
    Pitch = 15,
    SmoothingSeconds = 0.15f,
    CollisionRadius = 0.25f
});
CameraRig.Orbit(cameraObject, yawDegrees, pitchDegrees, 4);
CameraRig.BlendTo(cameraObject, vehicleObject, 1.0f);
CameraRig.Shake(cameraObject, amplitude: 0.15f, seconds: 0.4f);
```

Built-in examples, selectable as script classes:

- **CharacterCameraRig**: attach to Camera + Camera Rig, assign a character target,
  and use arrow keys to orbit. Character movement can come from a separate gameplay
  script; remove any competing camera control from that script.
- **VehicleCameraRig**: attach to Camera + Camera Rig and assign a moving vehicle.
  The camera follows its heading. Assign an optional alternate target and press C
  to blend to it; press H for an impact shake.

Settings serialize with scenes, prefabs, history, and play-change review. Blend and
shake timers are transient. Pausing (zero simulation delta or zero time scale)
freezes the camera and timers. Missing/inactive targets hold the last camera pose
and reset transient state; a valid target resumes from its desired pose. Runtime
start/stop clears transient state. Scene replacement cannot retain target pointers.

## Verification

`PlutoGECameraRigTests` checks follow/orbit positions and orientation, rotated
parents, disabled/missing targets, finite-value validation, 30/120 Hz smoothing,
blend timing, shake expiry and pause, authoring/runtime scheduling, time scale,
restart, sphere collision and trigger/hierarchy filtering, scene snapshots,
undo/redo, and prefab target remapping. `PlutoGECameraRigManagedTests` checks the
native packet layout, callback registration, command dispatch, and failure returns.

Interactive OpenGL/Vulkan checks remain pending: follow a moving character and
vehicle, orbit beside a wall, pause during a blend/shake, switch targets, and
stop/play again. Camera cuts between different rigs, FOV blending, and automatic
camera selection are outside this milestone's target-transition API.
