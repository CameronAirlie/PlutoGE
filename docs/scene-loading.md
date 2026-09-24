# Responsive scene transitions

Full runtime transitions now go through `core::SceneLoading`. The standalone
startup path, script-requested scene changes in both hosts, and editor Play
activation use the same loading lifecycle. Existing `SceneManager.LoadScene`
calls require no game changes. Benchmark/gameplay scene assets are not modified.

## Responsibilities

- `SceneLoading` owns Reading, Constructing, Activating, Complete and Failed
  states, error reporting, reentrancy rejection and loading-frame diagnostics.
  It accepts an activation callback and a presenter rather than owning host
  scenes or depending on a particular UI. The host keeps its old scene until
  construction succeeds.
- `platform::LoadingWork` is a scoped owner-thread loading pump. Checkpoints
  yield to loading presentation after approximately 8 ms of work between frames. They do not
  run the normal game/editor loop, gameplay scripts, physics or scene rendering. Nested
  presentation is suppressed and workers do not inherit the context.
- CPU-only file reading, uncached mesh-source parsing, and audio decoding,
  downmixing and PCM conversion run on a worker, with
  presentation continuing while the owner waits. Mesh cache publication, GPU
  resource creation, deserialization and activation remain on the owner thread.
- Scene records, hierarchy construction, navigation bake rows, script startup/teardown and audio
  preloading provide checkpoints. These are deliberately outside GPU command
  recording. Worker completion is joined before its captured resources die.
- `LoadingScreenRenderer` owns the scene-independent loading texture, embedded
  glyph geometry and animation. A small unlit shader and one reusable vertex
  buffer avoid initializing 3D materials, shadows or post-processing for loading.
  It accepts dimensions and style, uses Vulkan or
  OpenGL, and has no window, swapchain, editor or gameplay dependency.
- `LoadingScreenSession` owns one transition's output, optional RML document, and
  optional controller. Its `Render` is host independent; `Present` is the standalone
  adapter. Preview uses the same renderer with controller execution disabled.
  `RhiRenderService::RenderLoading` / `PresentLoading` remain lightweight built-in
  fallback helpers. Loading never replaces or resizes the editor host target.
- `RmlLoadingDocument` has an isolated noninteractive RmlUi context and RHI renderer.
  `RmlUiRuntime` owns global RmlUi initialization and shared fonts. Scene resets
  leave loading contexts alive. Context removal releases the RmlUi render-manager
  cache before its renderer is destroyed; global shutdown closes live contexts first.
- `EditorLoadingPresenter` supplies the editor host adapter. It renders at the
  Game viewport's pixel dimensions, then `EditorViewportOverlay` composites the
  result over that region of the completed editor frame. This also supports a
  detached Game viewport. A hidden/collapsed Game viewport does not trigger a
  full-window fallback.

The screen is indeterminate: the amount of serialized data does not predict mesh
decoding, resource upload or script startup cost. No fabricated percentage is
reported. `SceneLoading::Presenter` is the extension point for a branded screen;
keep its resources resident and avoid invoking gameplay from it. Hosts can supply
another presenter without changing transition policy or the default renderer.

## Editor frame boundary and lifetime

Play commands are queued; Play activation and script-requested transitions run
only after the current editor frame has been submitted. During the cooperative
loading pump, the editor replays that completed frame with a Game viewport
overlay. It does not call ImGui NewFrame, update panels, tick gameplay or read the
partially replaced scene. The surrounding editor remains visible but inactive.
Native events continue to be polled, including close and resize. The layout stays
frozen until the next normal frame, with clipping adjusted for the host size.

The overlay borrows completed draw lists for the synchronous presentation scope;
its temporary platform-window draw-data pointer is restored on success and
exceptions. Viewport textures and font resources remain resident during the
transition. Its loading texture registration is scoped to the transition. The
renderer persists across scene changes and is released before graphics-device
shutdown. OpenGL presentation invalidates the legacy state cache after offscreen
RHI work; Vulkan prepares registered textures through its normal compositor path.

Script input is suspended while loading. Queued editor input is discarded on
resume so clicks during loading cannot edit the new scene. Delta time is reset
after transitions. `Engine::PresentLoadingScreen` rejects editor hosts to prevent
accidental reintroduction of whole-window game loading screens.

## Authoring project loading screens

In **Content Browser > Create > Loading Screen**, name the screen and choose
**Create screen**. The editor creates a `.plutoloading` asset, an RML document,
matching RCSS stylesheet and portable font under `Assets/UI/Loading`.
Double-click a loading asset to reopen **Loading Screen Editor**. Edit the RML
and RCSS tabs, choose **Save and reload**, and enable **Animated preview** to
inspect stages and CSS animation. The source editor supports documents up to
256 KiB and edits the document's same-name stylesheet; additional linked files
can be authored externally and applied with **Reload from disk**. Preview runs
before editor GPU recording and does not execute project controllers.

Choose **Use as project loading screen**, or select the asset in **Project
Settings > Loading Screen**, then save the project. Empty selection uses the
built-in screen. Assets use this versioned format:

```ini
LoadingScreenVersion=1
Document=project://UI/loading.rml
Controller=Game.LoadingController
```

`Controller` may be empty. The project manifest persists the selected asset as
`LOADING_SCREEN`. `LOADING_TITLE` and `LOADING_ACCENT` remain fallback styling.
The asset document uses a project reference; relative RML/RCSS image and font
paths remain portable in standalone content packs. Include `@font-face` rules
in linked RCSS for custom fonts. Standard RCSS keyframes support spinning icons,
pulses, transforms, opacity and other RmlUi animations without any script:

```css
#spinner { animation: 1.2s linear infinite spin; }
@keyframes spin {
    from { transform: rotate(0deg); }
    to { transform: rotate(360deg); }
}
```

Optional element IDs `loading-stage`, `loading-scene` and `loading-error` receive
plain-text status before each controller update. Missing IDs are allowed. RML
assets render at the viewport's dimensions; use percentage positioning for
responsive layouts. Both RHI backends produce bottom-up loading textures, like
scene targets. Hosts flip sampling UVs; standalone Vulkan presentation applies
its normal scene-texture flip.

For scripted tips or animation, derive a concrete class from
`PlutoGE.ScriptCore.LoadingScreenController`, build scripts, and select its full
class name in the loading asset editor:

```csharp
public sealed class LoadingController : LoadingScreenController
{
    private float elapsed;
    protected override void OnLoadingStarted() { elapsed = 0; }
    protected override void OnLoadingUpdate(SceneLoadStatus status, float deltaTime)
    {
        elapsed += deltaTime; // Unscaled wall time between loading frames.
        Document.Element("tip").Markup = elapsed < 5 ? "Preparing..." : "Almost there...";
    }
    protected override void OnLoadingFinished() { }
}
```

The controller is ownerless, created once per transition and destroyed after
completion or failure. It is excluded from entity script pickers. Use its
`Document` to change text, classes, attributes and styles. The reserved
`loading://active` reference resolves only during loading callbacks. Do not
access scene entities, dispatch gameplay input, reload assemblies, or start a
nested transition from a loading callback. `OnLoadingFinished` denotes teardown,
including failure; consult `SceneManager.LoadingStatus` for the result. Controller
exceptions are reported and disable the controller/custom rendering for that
transition; invalid assets/documents fall back to the built-in renderer. A
controller initialization failure leaves an otherwise usable RML screen running.
Loading remains cooperative: a long indivisible task can delay both callbacks
and RCSS animation until the next checkpoint.

Embervault's example is `Assets/UI/loading.plutoloading`, with `loading.rml`,
`loading.rcss` and `EmbervaultLoadingScreen.cs`. The sigil animates in RCSS and its
controller rotates tips. No scene canvas or entity is needed.

## Managed API

```csharp
if (SceneManager.LoadScene("project://Scenes/Next.plutoscene"))
    transitioning = true;

// On subsequent updates, including when a transition fails:
var status = SceneManager.LoadingStatus;
if (status.Stage == SceneLoadStage.Failed)
    ShowTransitionError(status.Error);
```

Requests are accepted at a frame boundary; concurrent/nested requests are rejected.
Gameplay is suspended while the transition runs. The status remains available
after completion and across scene generations. Game-specific error UI should
clear its own transition mode and offer retry or return to menu. Construction
failure never invokes the activation callback. Activation callback exceptions
are reported, but rollback of arbitrary gameplay side effects is not promised.

## Responsiveness contract and expansion

This is cooperative loading, not permission to mutate a scene or use a graphics
context from a worker. A checkpoint interval is not a hard frame deadline: an
individual resource upload, navigation bake, driver operation or custom script
callback can still exceed it. Put future expensive CPU-only preparation behind
`LoadingWork::Prepare`, or split indivisible main-thread tasks into smaller units
and checkpoint only between valid units. Never checkpoint inside active GPU
recording or call the normal host loop recursively.

Window events continue to be polled during preparation; closing the window is
observed, but in-flight CPU preparation is joined rather than abandoned. The
current implementation does not expose interactive cancellation. Audio mixing
continues on its device thread. Host delta clocks are reset after transitions so
loading time is not integrated as a physics step. The standalone transition keeps
the loading frame visible until the next scene update has submitted its commands.
First-use rendering work after activation can still require separate prewarming.

## Validation

`PlutoGESceneLoadingTests` generates a temporary 2,000-entity fixture and exercises
worker isolation, owner-thread presentation, non-reentrant pumping, load stages,
failed reads/headers, retention of the old scene, activation errors and retry.
No shipping game scene is edited. Optional graphics runs present the loading
screen while CPU work executes and report frame count and longest interval:

```powershell
cmake --build out/build/msvc-nvidia --config Debug --target PlutoGEEditor PlutoGESceneLoadingTests
ctest --test-dir out/build/msvc-nvidia -C Debug -R '^PlutoGESceneLoadingTests$' --output-on-failure
$env:PLUTOGE_DISABLE_STREAMLINE='1'
$env:PLUTOGE_SHADER_ROOT=(Resolve-Path out/build/msvc-nvidia/engine/render/shaders).Path
./out/build/msvc-nvidia/tests/Debug/PlutoGESceneLoadingTests.exe --vulkan
./out/build/msvc-nvidia/tests/Debug/PlutoGESceneLoadingTests.exe --opengl
```

An optional second graphics argument writes a PPM capture for visual QA. An
optional third argument selects a project manifest to exercise its actual RML
and managed controller and writes a `.custom.ppm` capture. Graphics checks cover
asymmetric glyph orientation, RML orientation, RCSS animation, status binding,
controller lifecycle, scene-reset survival, preview isolation and invalid-asset
fallback. Asset tests cover versioning, round trips, duplicate keys and invalid
references.

### Development-machine results (24 September 2026)

- Debug editor/runtime builds passed. Loading lifecycle, additive streaming and
  audio lifecycle tests passed.
- Vulkan and OpenGL graphics fixtures passed pixel-readback checks for a visible
  label and changing animation/stages. Captures were visually checked for correct
  orientation. OpenGL initialization also logged a sampler-limit warning; loading
  screen rendering and readback passed despite that host diagnostic.
- Isolated DungeonCrawler native QA passed all 165 world/progression checks over
  two processes, including repeated Main/Forge/Forest transitions. Source gameplay
  scene hashes were unchanged, and startup validation still passed.
- The first implementation had a 1,549 ms maximum loading-frame interval at Title
  startup and 372–610 ms intervals during initial level transitions. After moving
  audio preparation to workers and checkpointing navigation rows, the repeated
  Debug run measured 240 ms at Title startup and 50–67 ms across level transitions.
  These are longest intervals between loading presentations, not total load time
  or a claim of a fixed frame rate. First-use initialization still has a brief stall.

## Project loading appearance

Projects without loading settings use a neutral grey LOADING label and animated
activity bar. Optional manifest fields customize startup, runtime transitions and
editor Play without loading scene resources:

```text
LOADING_TITLE	EMBERVAULT
LOADING_ACCENT	0.95	0.48	0.12
```

The title supports A-Z and spaces (lowercase is converted), up to 32 characters.
RGB accent channels are clamped to 0-1. Omit LOADING_TITLE for the default layout.
The settings survive project saves and standalone export. DungeonCrawler opts
into the Embervault title, amber accent and title divider in its own manifest.

### Viewport presentation regression tests

`PlutoGEEditorLoadingScreenTests` checks viewport clipping, texture orientation,
hidden views, host resize clipping and cleanup after presentation exceptions.
Optional graphics modes exercise the actual compositor without advancing ImGui
frames. The OpenGL main-window run reads back pixels and requires visible custom
branding, visible surrounding editor UI, no changed pixels outside the Game
viewport, and exact restoration after the overlay is removed. Vulkan and detached
window runs exercise texture registration, submission and draw-data lifetimes.

```powershell
cmake --build out/build/msvc-nvidia --config Debug --target PlutoGEEditorLoadingScreenTests PlutoGESceneLoadingTests
ctest --test-dir out/build/msvc-nvidia -C Debug -R '^(PlutoGEEditorLoadingScreenTests|PlutoGESceneLoadingTests|PlutoGEModelAssetTests)$' --output-on-failure
$env:PLUTOGE_DISABLE_STREAMLINE='1'
$env:PLUTOGE_SHADER_ROOT=(Resolve-Path out/build/msvc-nvidia/engine/render/shaders).Path
./out/build/msvc-nvidia/tests/Debug/PlutoGEEditorLoadingScreenTests.exe --opengl out/build/loading-editor-viewport.ppm
./out/build/msvc-nvidia/tests/Debug/PlutoGEEditorLoadingScreenTests.exe --vulkan
./out/build/msvc-nvidia/tests/Debug/PlutoGEEditorLoadingScreenTests.exe --opengl --detached
./out/build/msvc-nvidia/tests/Debug/PlutoGEEditorLoadingScreenTests.exe --vulkan --detached
```
