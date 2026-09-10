# Your first playable game

[Manual](../README.md) · Next: [Editor, assets, and worlds](world-building.md)

This exercise builds a small room with a keyboard-controlled capsule and a fixed
game camera. It establishes a working project before adding imported characters,
animation, navigation, or advanced lighting.

## 1. Create the project

Build and launch the editor using the [engine build instructions](../../README.md#build-and-run).
Install the .NET 8 SDK for gameplay authoring. Choose **File > New Project…** and
save `MyGame.plutoproject` in a dedicated directory. The editor generates a script
project and an `Assets` directory, including the initial scene and managed output.

Keep the manifest, script project, and Assets together when moving the project.
Use this additional folder organization as your game grows:

```text
Assets/
  Scenes/       Main.plutoscene
  Scripts/      SimpleWalker.cs
  Prefabs/
  Materials/
  Meshes/
  Audio/
  UI/
  Managed/      (compiled scripts)
```

The folders are a convention; asset references are relative to Assets. For
example, `project://Scenes/Main.plutoscene` identifies the scene without embedding
a developer's machine path. Save scenes through the editor instead of writing
their serialized records by hand.

## 2. Assemble a test room

Use the hierarchy to create entities and the Inspector to add components. The
following positions and dimensions are example authoring values, in world units.
Leave these entities unparented, with zero rotation and unit scale initially.

| Entity | Components | Setup |
| --- | --- | --- |
| Floor | Mesh, Collider | Cube mesh; scale `(12, 1, 12)`; position `(0, -0.5, 0)`; Box collider with local size `(1, 1, 1)` |
| Player | Mesh, Collider, Rigidbody, Script | Capsule visual if available, or a cube placeholder; position `(0, 1.1, 0)`; Capsule collider radius `0.4`, height `2`; Rigidbody kinematic, gravity off |
| Wall | Mesh, Collider | Cube mesh; position `(0, 1, -4)`; scale `(6, 2, 0.5)`; Box collider with local size `(1, 1, 1)` |
| Camera | Camera | Position `(0, 6, 8)`; rotate downward toward the origin; enable Main Camera |
| Sun | Light | Directional light; rotate to illuminate the floor and wall |

The floor and wall need no Rigidbody for this static level. Scale multiplies
collider dimensions: setting both the Floor scale and its collider size to
`(12, 1, 12)` would make its collision much larger than intended. Match the
Player's visible mesh to its collider without changing the root scale.

The game camera looks down local `-Z`; the editor's free camera is separate and
does not determine what the player sees. Adjust the Camera while watching the
Game viewport until the floor, player, and wall are visible.

## 3. Add movement

Create `Assets/Scripts/SimpleWalker.cs` with this complete class:

```csharp
using System.Numerics;
using PlutoGE.ScriptCore;

public sealed class SimpleWalker : ScriptBehaviour
{
    [SerializedField] private float speed = 4.0f;

    public override void OnUpdate(float deltaTime)
    {
        if (deltaTime <= 0.0f) return;

        Vector3 direction = Vector3.Zero;
        if (Input.IsKeyDown(KeyCode.W)) direction.Z -= 1.0f;
        if (Input.IsKeyDown(KeyCode.S)) direction.Z += 1.0f;
        if (Input.IsKeyDown(KeyCode.A)) direction.X -= 1.0f;
        if (Input.IsKeyDown(KeyCode.D)) direction.X += 1.0f;

        if (direction.LengthSquared() > 0.0f)
            Physics.MoveKinematic(GameObject,
                Vector3.Normalize(direction) * speed * deltaTime);
    }
}
```

Choose **Runtime > Build Scripts**. Resolve Console build errors, select Player,
and choose `SimpleWalker` in its Script component. Set Speed to `4`.

This example moves horizontally in world space. Normalizing direction prevents
diagonal movement from being faster. `MoveKinematic` sweeps against collision
geometry and returns the movement actually applied. It is a movement primitive;
this example deliberately supplies no gravity, jumping, stair handling, or ground
snapping. For a fuller controller, study the engine's
[KinematicFpsController](../../engine/scripting/managed/PlutoGE.ScriptCore/Examples/KinematicFpsController.cs).

## 4. Play and verify

Save the scene and project. Press **F5**, focus the Game viewport, and use WASD.
You should move at a consistent speed and stop at the wall. Press **Shift+F5** to
stop: normal stopping restores the authoring scene.

Verify these separately before adding more content:

1. The Game viewport renders through the authored camera.
2. WASD moves Player, including diagonally.
3. The wall blocks movement from both sides.
4. Stopping and starting returns Player to the authored spawn position.
5. Editing Speed, rebuilding when needed, and playing again changes movement.

For tuning during Play, **Runtime > Stop and Keep Changes...** lets you review
supported serialized property changes before keeping them. Save afterward. It
does not retain spawned entities or arbitrary managed state; see
[retention rules](../EDITOR_WORKFLOWS.md#keep-selected-play-mode-changes).

## 5. Add a follow camera

Once the fixed view works, add Camera Rig to Camera and assign Player as Target
Entity. Choose Follow mode, a Focus Offset around `(0, 0.5, 0)`, an Offset around
`(0, 4, 6)`, and Smoothing Seconds `0.15`. These are starting values to tune in
your room. The rig updates during Play, so a stationary edit-mode camera does
not mean it is broken.

Enable collision avoidance with a small positive Collision Radius if the camera
can move behind walls. Keep its focus outside solid geometry. See
[camera rigs](../CAMERA_RIGS.md) for orbit input, blending, and shake. Use one
transform driver on the camera; a follow script and a rig can fight each other.

## 6. Run outside the editor

Set the saved scene as the startup scene in Project Settings. With the development
runtime built, launch from the engine repository root:

```powershell
.\out\build\msvc\runtime\Debug\PlutoGERuntime.exe `
  "C:\Projects\MyGame\MyGame.plutoproject"
```

Replace the example project path with yours. Test the same movement and camera
checks here before proceeding to [shipping](game-systems.md#export-and-release-checks).

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Script class missing | Build output, inheritance from `ScriptBehaviour`, parameterless construction, and the configured managed assembly |
| Player does not move | Play mode, Game viewport input focus, active Player, enabled Script, and script assignment |
| Player passes through wall | Enabled non-trigger wall collider, collider dimensions, and Player collider/kinematic body setup |
| Black Game view | Enabled main camera on an active hierarchy, camera aim, visible mesh, light, and material |
| Edits disappear after Play | Normal Stop restores the scene; use the supported keep-changes workflow for tuning |
| Runtime differs from editor | Save the scene, verify startup scene, rebuild scripts, and compare project/render settings |
