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
  run the normal game/editor loop, scripts, physics or scene rendering. Nested
  presentation is suppressed and workers do not inherit the context.
- CPU-only file reading, uncached mesh-source parsing, and audio decoding,
  downmixing and PCM conversion run on a worker, with
  presentation continuing while the owner waits. Mesh cache publication, GPU
  resource creation, deserialization and activation remain on the owner thread.
- Scene records, hierarchy construction, navigation bake rows, script startup/teardown and audio
  preloading provide checkpoints. These are deliberately outside GPU command
  recording. Worker completion is joined before its captured resources die.
- `RhiRenderService::PresentLoading` supplies the default scene-independent
  screen: embedded LOADING glyph geometry, an animated activity bar and stage
  dots. It uses the host RHI on Vulkan and OpenGL. Editor compositing is suppressed
  during loading so old ImGui frames cannot cover the screen.

The screen is indeterminate: the amount of serialized data does not predict mesh
decoding, resource upload or script startup cost. No fabricated percentage is
reported. `SceneLoading::Presenter` is the extension point for a branded screen;
keep its resources resident and avoid invoking gameplay from it.

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

An optional second graphics argument writes a PPM capture for visual QA.

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
