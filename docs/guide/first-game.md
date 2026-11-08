# Your first playable game

[Manual](../README.md) · Next: [Editor, assets, and worlds](world-building.md)

Start from a template so you have something playable within a minute, then tune it, add ready-made gameplay
pieces and write your first script. The last section builds movement from an empty project, for when you want to
understand every piece.

## 1. Create a project from a template

Build and launch the editor using the [engine build instructions](../../README.md#build-and-run), and install the
.NET 8 SDK for scripts. Without a project open, the editor shows the project launcher; otherwise choose
**File > New Project from Template**. Pick **Third-person game** and save `MyGame.plutoproject` in an empty
folder. The editor creates the project, builds its scripts once and opens the starter scene.

| Entity | What it does |
| --- | --- |
| Floor, Wall | Static boxes with colliders to walk on and bump into |
| Sun | Directional light |
| Player | Capsule collider, kinematic rigidbody and the `ThirdPersonController` script, plus health, inventory and save support |
| Camera | Child of Player; the controller drives it from the mouse |
| Project menu | Escape opens a pause and settings menu; F6 and F9 save and load |

The other templates start other kinds of game the same way:

| Template | Starts with |
| --- | --- |
| First-person game | The same room with a first-person controller and hitscan |
| Vehicle game | A raycast-suspension car, chase camera and menu |
| Object viewer | A camera that orbits, zooms and pans around a model |
| UI application | A camera and a localized settings interface |
| Empty | An empty scene and the C# project, for building from scratch (section 6) |

Keep the `.plutoproject` file, the script project and `Assets` together; the folder can go into version control
as it is (see [projects in version control](../EDITOR_WORKFLOWS.md#projects-in-version-control)).

## 2. Play it

Press **F5**. The Game view takes the keyboard and mouse straight away: **WASD** moves, the mouse turns the camera,
**Shift** sprints, **Space** jumps and **Escape** opens the menu. Press **Shift+F5** to stop.

Stopping restores the scene as it was before Play, so experiments are safe. To keep values you tuned during Play,
use **Play > Stop and Keep Changes...** and choose which changes to keep.

## 3. Tune the player

Select **Player** in the Scene Hierarchy. In the Inspector, the `ThirdPersonController` script lists its settings:
Walk Speed, Sprint Speed, Jump Height, Camera Distance and so on. Change **Jump Height** to `3` and play again.

Useful while you explore:

- Type in the Inspector's **Search** box to find a setting: "jump" shows only the jump settings.
- Press **F** to frame the selection in the Editor Viewport, **Alt + left drag** to orbit around it and the mouse
  wheel to move closer. Hold the right mouse button and use **WASD** to fly.
- Press **Ctrl+P** to find any editor command by name, or to open an asset by typing part of its name.
- Every edit can be undone with **Ctrl+Z**. Unsaved scenes are recovered after a crash.

## 4. Add gameplay pieces

**Scene > Insert Gameplay Kit** adds ready-made pieces to the open scene, each as one undoable step:

| Kit | Use |
| --- | --- |
| Inventory pickup | Walk into it to add an item to the Player's inventory |
| Interactive door | Press **E** near it to open it |
| Checkpoint | A trigger volume that becomes the respawn point |
| Pause and settings menu | The same menu the template already has, for other scenes |

Insert an **Inventory pickup**, move it with the gizmo (**W**, **E** and **R** switch between move, rotate and
scale) and play. The kits are ordinary entities with scripts, so their settings are in the Inspector.
[Faster authoring workflows](../EDITOR_AUTHORING.md) describes each kit's settings.

## 5. Write a script

Make a coin that spins and disappears when the player touches it.

1. Create the coin: in the Scene Hierarchy choose **Create > Cube**, rename it `Coin`, set its scale to
   `(0.4, 0.4, 0.1)` and place it above the floor near the player.
2. Add a **Collider** component and tick **Is Trigger**, so the player passes through it and the script is told.
3. Add a **Script** component, choose **New**, name it `Coin` and choose **Create**. This writes
   `Assets/Scripts/Coin.cs`. **Open** opens it in your code editor (set yours in
   **Edit > Preferences > External Tools**).

Replace the file's contents with:

```csharp
using System.Numerics;
using PlutoGE.ScriptCore;

public sealed class Coin : ScriptBehaviour
{
    [SerializedField, Range(0, 720), Tooltip("Turn speed in degrees per second")]
    private float spinSpeed = 180.0f;

    [SerializedField] private int value = 1;

    public override void OnUpdate(float deltaTime)
    {
        GameObject.Rotation += new Vector3(0.0f, spinSpeed * deltaTime, 0.0f);
    }

    public override void OnTriggerEnter(GameObject other)
    {
        if (!other.HasTag("Player"))
            return;
        Debug.Log($"Collected a coin worth {value}", GameObject);
        GameObject.Destroy();
    }
}
```

Save the file. The editor rebuilds changed scripts automatically (**Scripts > Automatically Build Changed
Scripts**); compiler errors appear in the Console, and double-clicking one opens the file at that line. Select the
coin, check that its Script component shows `Coin` with Spin Speed and Value, and play. Walk into the coin: it
disappears and the Console shows the message. Double-clicking the message selects the coin that logged it.

Duplicate the coin with **Ctrl+D** to place more. To reuse it across scenes, right-click it in the Scene Hierarchy
and choose **Save As Prefab** (it goes to `Assets/Prefabs`); edits to the prefab then reach every placed coin
([prefab variants](../PREFAB_VARIANTS.md)).

If a script throws an exception during Play, the Console shows the script, the entity and the line, and
**Error Pause** in the Console toolbar pauses Play at the first one. The full API is in
[C# scripting](../CSHARP_SCRIPTING.md).

## 6. From an empty project

Building the same kind of game from the **Empty** template shows what the template set up for you. Create these
entities with the Scene Hierarchy's **Create** menu and the Inspector (positions and sizes in metres):

| Entity | Components | Setup |
| --- | --- | --- |
| Floor | Mesh, Collider | Cube; scale `(12, 1, 12)`; position `(0, -0.5, 0)`; Box collider size `(1, 1, 1)` |
| Player | Mesh, Collider, Rigidbody, Script | Cube placeholder; position `(0, 1.1, 0)`; Capsule collider radius `0.4`, height `2`; Rigidbody kinematic, gravity off; tag `Player` |
| Wall | Mesh, Collider | Cube; position `(0, 1, -4)`; scale `(6, 2, 0.5)`; Box collider size `(1, 1, 1)` |
| Camera | Camera | Position `(0, 6, 8)`, tilted down toward the origin; Main Camera on |
| Sun | Light | Directional; rotate it to light the floor |

Scale multiplies collider sizes, so leave collider sizes at `(1, 1, 1)` on scaled boxes. The game camera looks
down its local `-Z`; the Editor Viewport's camera is separate and does not affect the game.

Give the Player a new script named `SimpleWalker`:

```csharp
using System.Numerics;
using PlutoGE.ScriptCore;

public sealed class SimpleWalker : ScriptBehaviour
{
    [SerializedField] private float speed = 4.0f;

    public override void OnUpdate(float deltaTime)
    {
        Vector3 direction = Vector3.Zero;
        if (Input.IsKeyDown(KeyCode.W)) direction.Z -= 1.0f;
        if (Input.IsKeyDown(KeyCode.S)) direction.Z += 1.0f;
        if (Input.IsKeyDown(KeyCode.A)) direction.X -= 1.0f;
        if (Input.IsKeyDown(KeyCode.D)) direction.X += 1.0f;

        if (direction.LengthSquared() > 0.0f)
            Physics.MoveKinematic(GameObject, Vector3.Normalize(direction) * speed * deltaTime);
    }
}
```

`MoveKinematic` sweeps against colliders and returns the movement actually applied, so the wall stops the player.
It has no gravity, jumping or stairs; compare the template's
[ThirdPersonController](../../engine/scripting/managed/PlutoGE.ScriptCore/Examples/ThirdPersonController.cs) or
[KinematicFpsController](../../engine/scripting/managed/PlutoGE.ScriptCore/Examples/KinematicFpsController.cs) for
those. For a following camera, add a **Camera Rig** to the Camera and set Player as its target
([camera rigs](../CAMERA_RIGS.md)).

## 7. Run outside the editor

Choose the startup scene in **File > Project Settings**, then **Build > Build and Run Project...** (Ctrl+Shift+B).
The build is checked before it is written, and the location is remembered for next time. Development builds keep
logging and diagnostics; switch to Shipping in Project Settings for release. See
[shipping](game-systems.md#export-and-release-checks) before you share a build.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Script missing from the Script component | The Console for compiler errors; the class derives from `ScriptBehaviour` and has a parameterless constructor |
| Player does not move | The Game view has focus (click it), the Player and its Script are enabled |
| Trigger does nothing | The collider has Is Trigger ticked; the player has a rigidbody (kinematic is fine) and the `Player` tag |
| Player passes through a wall | The wall has a collider that is not a trigger, sized `(1, 1, 1)` on a scaled box |
| Black Game view | An active camera with Main Camera on, pointed at something lit |
| Edits vanish after Play | Stop restores the scene; use Stop and Keep Changes to keep tuned values |
| The build differs from the editor | Save the scene, check the startup scene and build again |
