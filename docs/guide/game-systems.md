# UI, saves, networking, and shipping

[Manual](../README.md) · Previous: [Presentation](presentation.md)

## Choose a UI workflow

PlutoGE provides native entity-based UI and document-based RmlUi. Use native
Canvas/Rect Transform/Image/Text/Button components for a small scene-authored HUD.
Use an RmlUi Canvas with `.rml` markup and `.rcss` styling for document layouts.
These have different authoring models; an RmlUi document does not require a child
entity for every text label or button.

### Native HUD example

Create a Canvas entity. Under it, create a label with Rect Transform and UI Text.
Choose a top-left anchor, give it a visible size and margin, and set initial text
and font size. Use Canvas scale/sorting controls to establish presentation, then
test in the Game viewport at more than one resolution.

Attach this complete class to an active entity and assign the label GameObject:

```csharp
using PlutoGE.ScriptCore;

public sealed class ScoreHud : ScriptBehaviour
{
    [SerializedField] private GameObject? label;
    private UITextComponent? text;
    private int score;

    public override void OnCreate()
    {
        text = label?.GetComponent<UITextComponent>();
        Refresh();
    }

    public void AddScore(int amount)
    {
        score += amount;
        Refresh();
    }

    private void Refresh()
    {
        if (text is not null) text.Text = $"Score: {score}";
    }
}
```

Another gameplay script can find a `ScoreHud` on its assigned HUD entity and call
`AddScore(10)` when awarding points. This example updates on changes, avoiding
string formatting every frame. UIImage Fill Amount supports bars; UIButton
provides Interactable and frame states such as WasClicked. Read a button's click
state in `OnUpdate` and perform the action once.

If the HUD is missing, check active hierarchy, Canvas, Rect Transform size/anchor,
text color, and sorting. Test long values and narrow windows before considering
the layout finished.

### RmlUi menu example

Follow the [RmlUi quick start](../RMLUI_QUICKSTART.md) to create `UI/hello.rml`, its
sibling stylesheet, and a project font. Add an **RmlUi Canvas**, assign that document,
and choose Scale With Screen Size with a reference resolution appropriate to the
game. Font files must be available through the project; do not assume a system
font such as Arial is built in.

Give scripted elements stable IDs. This complete controller uses the quick-start
document's `continue-button` and loads a saved Game scene:

```csharp
using PlutoGE.ScriptCore;

public sealed class StartMenu : ScriptBehaviour
{
    private RmlDocument? document;

    public override void OnCreate()
    {
        Input.CursorLocked = false;
        document = new RmlDocument("project://UI/hello.rml");
        document.OnClick("continue-button", StartGame);
    }

    private void StartGame()
    {
        if (!SceneManager.LoadScene("project://Scenes/Game.plutoscene"))
            Debug.LogWarning("Could not request the Game scene.");
    }

    public override void OnDestroy() => document?.Dispose();
}
```

Save `Game.plutoscene` first and ensure it has a camera and gameplay setup. The
Canvas path and controller document path must identify the same document. RML/RCSS
asset URLs are relative to their containing files. Test fonts, images, style paths,
and clicks again after export. An RML Widget component is a separate integration
option; ordinary document canvases need no widget children. See
[RmlUi integration](../RMLUI_INTEGRATION.md) for the broader API.

## Scene flow and pause

`SceneManager.LoadScene` requests a transition at the end of the current frame.
Its boolean reports request acceptance, not a completed transition. Keep the
startup scene in Project Settings synchronized with your intended entry point.

**Example game flow:** start in MainMenu, request Game on Continue, and request
MainMenu again from the pause menu. Cache references within each scene and
reacquire them when the next scene starts. Entity references and runtime IDs are
not a save-game identity scheme across scene replacements.

`GamePause.TimeScale = 0` pauses simulation time. Preserve and restore the previous
scale on resume, including when leaving a paused scene. Keep menu input responsive
while gameplay scripts avoid actions for zero simulation delta. Do not place an
unconditional zero-delta return at the top of a menu script that must unpause.

Study the built-in
[RmlPauseMenuController](../../engine/scripting/managed/PlutoGE.ScriptCore/Examples/RmlPauseMenuController.cs)
for pause, cursor, scene, and cleanup coordination. `GamePause.IsPaused` is readable
from game scripts, but its setter is internal to ScriptCore; user scripts should
not copy that internal assignment into their own project. Simulation pause is not
automatically a policy for networking, audio, or every gameplay input handler.

## Save progress and settings

Use `ProjectStorage` to distinguish authored assets from writable per-user data.
Store progress in user data, since exported/installed Assets may be read-only.
Paths are relative to the selected storage root; escaping that root with `..` or
supplying absolute paths is rejected. Writes create directories and atomically
replace the destination, but loading can still fail due to corruption or old data.

This complete utility stores a small versioned record with JSON properties:

```csharp
using System;
using System.IO;
using System.Text.Json;
using PlutoGE.ScriptCore;

public sealed class ProgressData
{
    public int Version { get; set; } = 1;
    public int BestScore { get; set; }
}

public static class ProgressStore
{
    private const string Path = "Saves/progress.json";

    public static ProgressData Load()
    {
        try
        {
            if (!ProjectStorage.UserDataFileExists(Path)) return new();
            var data = ProjectStorage.ReadUserDataJson<ProgressData>(Path);
            if (data is not null && data.Version == 1 && data.BestScore >= 0)
                return data;
            Debug.LogWarning("Unsupported or invalid progress data.");
        }
        catch (Exception error) when (error is IOException
            or UnauthorizedAccessException or JsonException)
        {
            Debug.LogWarning($"Progress could not be loaded: {error.Message}");
        }
        return new();
    }

    public static bool Save(ProgressData data)
    {
        try
        {
            ProjectStorage.WriteUserDataJson(Path, data);
            return true;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            Debug.LogWarning($"Progress could not be saved: {error.Message}");
            return false;
        }
    }
}
```

This file is a utility, so it is not attached as a Script component. A behaviour
calls `ProgressStore.Load()` at an appropriate point, updates BestScore after a
completed run, and calls Save explicitly. Surface save failures in the game's UI
where progress matters. The fallback load does not overwrite the original file;
decide how to handle incompatible versions before saving over them.

Use properties or explicit JSON options for custom records. Save persistent game
identifiers and values, then reconstruct entities; do not serialize native wrapper
objects as a substitute for game-state design. Test missing, malformed, and older
save files, and verify a save survives quitting and reopening the exported game.

## Networking

The managed Networking namespace provides a multi-client TCP server and client,
reliable framed messages, channel IDs, and binary/string/JSON payloads. Background
socket work queues events; call `Poll()` on the gameplay thread to dispatch them.
Polling is required even when connected sockets appear healthy.

**Example: a local multiplayer prototype.** Run one host and a second game process
as client on `127.0.0.1:7777`. Start with a single text message, verify receipt and
disconnect, then add application messages. Use the complete
[MultiplayerChat example](../../engine/scripting/managed/PlutoGE.ScriptCore/Examples/MultiplayerChat.cs)
and [networking guide](../NETWORKING.md) for connection and message code.

Choose channels by message purpose, for example chat and gameplay commands. Check
send results, bound payloads, and validate message fields before acting. The default
maximum payload is 1 MiB. Dispose client/server resources in `OnDestroy` and handle
connection-task failures rather than leaving an unobserved asynchronous operation.
If callbacks never arrive, check Poll, connection errors, address/port, and whether
the host process is still running.

Transport is not automatic replication: define authority, entity identifiers,
spawn/despawn messages, state synchronization, interpolation, and late-join
behavior for your game. Server-side gameplay should validate client requests.
Consult the networking guide's current scope before assuming matchmaking,
authentication, encryption, prediction, or automatic physics synchronization.

## Debugging and performance

Use Console messages for state transitions and failures; avoid logging every frame
for every entity. Editor DebugDraw helps inspect rays and paths. Validate saved
assets with **Edit > Project Validation...**, build scripts first, and rerun after
fixing results. Validation finds particular structural/reference problems, not
all gameplay bugs.

For performance, open Profiler, Record a representative play session, Stop, and
inspect a slow frame. Use Timeline/Hierarchy to distinguish scripts, physics,
animation, UI, and rendering. GPU observations can arrive later than their CPU
frame; they are not an exact same-frame execution timeline. See
[Profiler](../PROFILER.md) for interpreting captures.

**Example: a forest hitch.** Compare the same route before and after increasing
foliage density. If physics dominates, inspect collision density/cells; if rendering
dominates, inspect draw distance, shadows, mesh detail, and transparency. Change
one setting and repeat the route. For frequent spawn hitches, compare prefab
preloading and the amount of work performed in spawned scripts' OnCreate.

A 60 FPS target allows approximately 16.7 ms per frame; 30 FPS allows 33.3 ms.
Measure a representative standalone optimized build as well as the editor:
editor UI, debug overlays, and profiling add costs. Standalone renderer profiling
is disabled automatically, so do not expect all editor metrics there.

## Export and release checks

Save scene and shared asset edits, select the startup scene, and build scripts.
The editor's **Build Project** and **Build and Run Project** validate saved data
before export; errors block the build and warnings allow it. Fix issues and rerun,
including missing assets in scenes that are not currently open.

For a Windows shipping folder, run from the engine repository root:

```powershell
.\tools\Export-Game.ps1 `
  "C:\Projects\MyGame\MyGame.plutoproject" `
  "C:\Builds\MyGame\MyGame.exe"
```

The helper reuses a prebuilt Release runtime, builds an adjacent script project
when present, cooks assets, copies native dependencies, and bundles .NET. Add
`-RebuildRuntime` on the first export or after engine changes, or use
`-RuntimePath` to select an existing shipping runtime. Follow
[Exporting](../../EXPORTING.md) for the exact workflow and lower-level command.
Distribute the entire generated folder. The `.plutopack` is packaging, not
cryptographic protection. The PowerShell helper describes the Windows workflow;
do not assume an executable exported here runs on Linux.

Test the generated game from a different working directory and, where possible,
a clean machine without the editor or SDK installed:

1. Launch into the intended startup scene with the expected camera and resolution.
2. Exercise movement, collisions, animation, effects, sound, menus, and pause/resume.
3. Load every scene and spawn representative prefabs, especially assets selected
   through dynamically constructed script paths.
4. Confirm fonts, textures, imported animations, and bake data are present.
5. Save, quit, relaunch, and load progress from user data.
6. Test network connection/disconnection if used, and measure the busiest game area.

Reference scanning cannot infer every dynamically computed asset path. When using
pruned cooking, verify that runtime-selected content is included by the supported
cooking workflow; a clean static reference report is not proof. Diagnose startup
failures with `PlutoGERuntime.log` beside the Windows executable and the configured
startup scene/managed assembly.
