# Usability and workflow plan

Started 2026-11-08. Goal: remove day-to-day awkwardness from making games in PlutoGE — surface errors, make the editor
behave the way Unity/Godot/Unreal users expect, fill gaps in the C# API that games currently work around, and make
project data robust under version control.

Evidence for each item came from an audit of the engine and the ThirdPersonProject sample
(`S:/PlutoGE/PlutoGE_Samples/ThirdPersonProject`), whose workarounds show where the engine falls short.

## Principles

- **Modular.** New behaviour goes behind a small interface or registry (for example a command registry, a document
  interface, a log-entry model) rather than special cases inside `EditorShell`. Engine libraries keep their existing
  dependency direction (`engine/ui` has no scene/renderer dependency, ScriptCore stays host-agnostic).
- **Match the existing editor.** Reuse the theme, the panel conventions, the content-browser command style and the
  existing preference storage. No new visual language.
- **Backward compatible data.** Old scenes, prefabs, projects and `.plutoscriptable` files keep loading. Format
  changes are additive or versioned.
- **Tested.** Each item adds or extends a test target where the code is testable headlessly; build sequentially in
  `out/build/msvc17` (never two builds at once).
- **Documented.** User-visible changes update the relevant guide under `docs/`.

Status: `[ ]` not started, `[~]` in progress, `[x]` done, `[-]` deferred (with reason).

---

## Tier 1 — Errors and editor correctness (quick, high impact)

- [x] **T1.1 Script exceptions reach the user.** Callback exceptions (`OnCreate/OnUpdate/OnFixedUpdate/OnLateUpdate/
  OnDestroy/collision/animation event`) are stored in `_lastError` and ignored natively. Log the full exception with
  entity and class through the script log sink, throttle repeats per instance, and stop calling `OnUpdate` family on an
  instance whose `OnCreate` failed. Optional "Pause on script error" editor toggle.
- [x] **T1.2 Structured Console.** Replace the single text blob with structured entries (severity, source, timestamp,
  repeat count, optional entity / asset / file:line), colour per severity, collapse duplicates, counts in the toolbar,
  clipped rendering, working auto-scroll, copy selection, double-click navigation.
- [x] **T1.3 Compile diagnostics to Console + open at line.** Parse `dotnet build` diagnostics into Console entries with
  file:line; add an "External script editor" preference (VS Code / Visual Studio / Rider / system default) that opens
  at the line.
- [x] **T1.4 Panel-scoped hotkeys.** Viewport W/E/R/F only when the viewport is hovered/focused; hierarchy Ctrl+G only
  when the hierarchy is focused; Backspace no longer deletes entities on Windows; Esc only clears selection from
  scene panels.
- [x] **T1.5 Asset editor documents.** An `IEditorDocument` interface (title, dirty, save, revert) implemented by the
  asset editors; the shell asks all dirty documents before closing / switching project; editors prompt before
  discarding edits when switching asset; Ctrl+S saves the focused document. Save / Don't Save / Cancel modal.
- [x] **T1.6 Small editor fixes.**
  - New root entities spawn in front of the editor camera.
  - Viewport drag-drop places on release (Shift keeps stamping).
  - Frame Selected uses combined world bounds of the selection and descendants.
  - Copy/paste supports multi-selection; duplicates get numbered names ("Crate (1)").
  - Game view takes focus when Play starts (preference).
  - Recovery window only after an unclean exit.

## Tier 2 — C# scripting API

- [x] **T2.1 Two-phase start.** Create every script instance and apply fields first, then call `OnCreate` on each, so
  sibling lookups in `OnCreate` succeed. Applies to runtime start and to instantiated prefabs.
- [x] **T2.2 Instantiate with configuration.** `Prefab.Instantiate<T>(position, rotation, parent, configure)` where
  `configure` runs before `OnCreate`; Quaternion and parent overloads.
- [x] **T2.3 Serialized field types and attributes.** Enums, Quaternion, Color, primitive arrays/lists, warning on
  unsupported members, `[Range]`, `[Tooltip]`, `[Header]`, `[FormerlySerializedAs]`. Existing `.plutoscriptable` files
  pick up new fields with their C# default values.
- [x] **T2.4 Time and timers.** Static `Time` (deltaTime, unscaledDeltaTime, time, unscaledTime, frameCount,
  timeScale) and `Timers.After/Every` bound to the owning behaviour's lifetime.
- [x] **T2.5 Behaviour enable/disable.** `ScriptBehaviour.Enabled`, `OnEnable/OnDisable`, callbacks skipped while
  disabled.
- [x] **T2.6 Physics queries and triggers.** Overlap sphere/box, SphereCast, RaycastAll, layer masks, trigger callbacks,
  contact data on collisions.
- [x] **T2.7 GameObject ergonomics.** Value equality, `IsAlive`, public `GameObject` on behaviours, quaternion world
  rotation, `GetComponentInParent/InChildren`.
- [x] **T2.8 Fire-and-forget audio.** `Audio.PlayAt / PlayOneShot` on an engine voice pool.
- [x] **T2.9 Input action gaps.** Unclamped delta axes, scroll binding, `WasReleased`, Vector2 composites, engine-driven
  map updates.
- [x] **T2.10 Play session hygiene.** `Application.PlayStarted/PlayStopped` events, engine statics reset on stop,
  `[ResetOnPlay]` for user statics.
- [x] **T2.11 Debugging.** `Debug.LogException(ex, context)`, context entity on logs (click to select), documented
  debugger attach, ScriptCore XML docs for IntelliSense.

## Tier 3 — Project data and pipeline

- [x] **T3.1 Prefab instances by reference.** Store instances as prefab reference + overrides and resolve on load so
  unopened scenes pick up prefab changes.
- [-] **T3.2 Bake data out of scene text.** Baked lighting / captures written to binary side files, scene stores a
  reference; legacy inline data still loads.
- [x] **T3.3 Diff-friendly scene and project files.** Stable entity IDs, shortest round-trip float formatting, editor
  state moved out of `.plutoproject` into per-user editor storage.
- [x] **T3.4 VCS-ready new projects.** Generate `.gitignore` / `.gitattributes`; csproj references the engine through
  a property, keeps a user-owned import for customisation.
- [x] **T3.5 Build settings and profiles.** Development / Shipping profiles, Release scripts for shipping, remembered
  output folder, product name / version / icon, every failure logged to the Console.
- [x] **T3.6 Hot reload beyond models.** File watching for textures, materials and audio with live reload.

## Tier 4 — Comfort and discoverability

- [x] **T4.1 Command registry, shortcuts list, command palette.** One registry for editor commands with shortcut,
  rebinding in Preferences, Help > Keyboard Shortcuts, Ctrl+P palette with quick-open.
- [x] **T4.2 Viewport navigation.** Alt+LMB orbit around the selection, wheel dolly in perspective, hold-Ctrl snap,
  speed toast, persisted gizmo/snap settings.
- [x] **T4.3 Inspector component actions.** Copy / Paste values, Reset, Move Up / Down, property search.
- [x] **T4.4 Docs reorganisation.** First-game guide rewritten around templates; engineering notes moved to
  `docs/dev/`; orphan guides linked from the manual.

---

## Progress log

Record each completed item here with the date, what changed, and how it was verified.

### 2026-11-08 — T1.1, T1.2, T1.3

- **Script exceptions (T1.1).** `scripting::ScriptErrorReport` + `ScriptErrorThrottle` (`ScriptErrors.h`): every
  managed callback failure is reported with class, callback, entity and full trace, throttled to the 1st, 10th,
  100th, ... occurrence. `ScriptInstance::IsFaulted()` stops per-frame/event callbacks after a failed `OnCreate`
  (`ScriptComponent::HasLiveInstance`). Hosts without a listener print to the script log. Script logging moved to
  `ScriptLogging.cpp`.
- **Console (T1.2).** `ConsoleLog` model (thread-safe, bounded, merges consecutive repeats, per-source removal,
  collapse helper) replaces the string vector. The panel is a clipped table with severity/source filters and
  counts, collapse, Error Pause, resizable detail pane, context menu, Ctrl+C and double-click navigation via
  `EditorShell::NavigateToConsoleEntry`. Console methods moved to `EditorConsole.cpp`.
- **Compiler diagnostics (T1.3).** `ScriptDiagnostics` parses `dotnet build` output (deduplicated) and .NET stack
  frames (preferring project files). Builds no longer echo raw output into the Console
  (`ScriptBuildConfig::echoOutputToLog`); each build replaces the previous compiler entries.
  `ExternalScriptEditor` + **Preferences > External Tools** open files at a line through VS Code / Visual Studio
  / Rider presets or a custom command, launched without a shell (`SystemShell::LaunchDetached`).
- **Shared infrastructure.** `EditorSettings`: per-user key/value settings (`editor-settings.ini`), flushed once
  per frame; later items reuse it.
- **Verified.** New `PlutoGEEditorWorkflowTests` (log model, settings round trip, diagnostics and stack parsing,
  command expansion, throttling) and `PlutoGEScriptBehaviourTests` (real managed exceptions from OnCreate and
  OnUpdate reach the listener with entity context and stack trace; faulted state; throttle 1, 10) pass.
  `PlutoGEEditorUI` builds. The editor executable could not be relinked while an editor instance was open, so
  the panel has not yet been checked visually.

### 2026-11-08 — T1.4

- `EditorShell::IsSceneShortcutContext()`: scene keys (Esc, Ctrl+C/V/D, Delete, Ctrl+G) act only when the
  hierarchy or editor viewport has focus, the pointer is over the editor viewport, or nothing has focus. Undo/redo
  and F5 stay global. Backspace no longer deletes entities or foliage instances.
- Viewport W/E/R/F need the viewport hovered or focused (keeps the hover-to-use behaviour from 434a4137) and no
  Ctrl. F frames from the hierarchy too.
- Verified: `PlutoGEEditorUI` builds. Interactive check pending until the editor can be relinked.

### 2026-11-08 — T1.5

- `IEditorDocument` (`EditorDocument.h`): name, dirty, save, discard, focus. Implemented by the Material,
  Particle System, Shader Graph, Animation Clip, Loading Screen, Animation Graph, Mesh, Foliage, Input Mapping,
  Post-Process, UI Theme and RML document editors, plus `EditorShell::SceneDocument` for scene + project. The
  shell discovers documents through `PanelManager::GetPanels()`, so the hand-maintained list in
  `ConfirmContinueWithUnsavedChanges` is gone.
- `ResolveUnsavedDocuments` asks once (Save / Don't Save / Cancel; MessageBox on Windows, zenity/kdialog on Linux)
  and stops on the first failed save, focusing that editor.
- `UnsavedSwitchGuard` generalises the Mesh editor's follow-the-selection prompt; the Material, Particle,
  Shader Graph, Animation Clip and Loading Screen editors no longer drop edits when the selection changes. Their
  save logic moved from button handlers into `SaveDocument`.
- Ctrl+S saves the focused document first (`EditorShell::SaveFocusedDocument`).
- Sequencer edits apply to the scene as undoable edits, so it is covered by the scene document.
- Not done here: undo/redo for the Material, Particle and Loading Screen editors (still revert-only).
- Verified: `PlutoGEEditorWorkflowTests` covers the resolver (cancel, save failure, save all, discard, nothing
  dirty) and the guard's follow rules; `PlutoGEEditorUI` builds.

### 2026-11-08 — T1.6

- Spawn in view: `EditorShell::GetSpawnPointInView()` (physics raycast along the view centre, 50 m, else 8 m
  ahead). Hierarchy presets opt out with `EntityPresetDefinition::placeInView = false` (sky, ocean, terrain,
  directional light).
- Drop to place: `PlacementState::placeOnDrop` stamps once on drag delivery; Shift on release keeps the existing
  persistent placement tool.
- Frame Selected: world AABB over mesh bounds of the selection and descendants; `EditorViewportCamera::flyTarget`
  glides the camera (cancelled by look/pan); `focusPoint` recorded for orbiting (T4.2).
- Clipboard holds every selected root; paste re-selects them all. `NextDuplicateName` ("Crate (1)").
- Game view focused on Play (`play.focusGameView`, new **Preferences > Behaviour** page, which also exposes
  Pause on script error). `Panel::Focus()` added.
- Recovery: `SceneRecovery::BeginSession/EndSession` session marker; the window auto-opens only after an
  unclean exit.
- Verified: `PlutoGEEditorWorkflowTests` (duplicate naming, session marker) pass; `PlutoGEEditorUI` builds.
  Editor binary still locked by the running editor, so Tier 1 still needs an interactive pass.

### 2026-11-08 — T2.1, T2.2

- `ScriptComponent::Prepare()` creates and configures an instance without OnCreate; `StartHierarchy` /
  `PrepareHierarchy` run the two phases over a subtree. `Scene::StartRuntime` prepares every script before any
  OnCreate.
- Optional bridge API `RegisterPrefabSpawnApi` (`InstantiatePrefabPrepared(reference, parentId)`,
  `StartEntityScripts(id)`); older hosts fall back to the previous behaviour.
- `Prefab.Instantiate` overloads now place the object before OnCreate (previously Position was set after);
  new `Instantiate(position, Quaternion, parent)` and `Instantiate<T>(..., configure, parent)`.
- Verified: `PlutoGEScriptBehaviourTests` runs a scene with Play started: a script created before its partner sees
  the partner's configured field in OnCreate, and a spawned prefab's OnCreate sees the configured value, local
  position and parent.

### 2026-11-08 — T2.3

- Field types appended to `ScriptFieldType`: Enum (stored by member name), Color, Vector4, Quaternion; `glm::vec4`
  appended to `ScriptFieldValue`. Arrays of any plain value type (bool, numbers, string, vectors, enum, colour).
- One text codec (`ScriptFieldText.cpp`: `FormatFieldValueText` / `ParseFieldValueText` / `InferFieldType`)
  replaces the three copies in `HostFxrScriptRuntime`, `ScriptComponent` and `ContentBrowserPanel`. Floats are
  written shortest-round-trip, which also removes "17.700001" noise from scene and scriptable files.
- Managed attributes `[Header]`, `[Tooltip]`, `[Range]`, `[FormerlySerializedAs]` (`InspectorAttributes.cs`) and a
  `Color` struct. They travel as optional metadata columns 9-14, so older hosts ignore them.
  `MigrateRenamedFields` moves values saved under former names (scenes, prefabs, scriptable assets, live apply).
- Unsupported `[SerializedField]` members now log a warning when scripts load.
- `DrawScriptField` (`ScriptFieldEditor.cpp`) is the single field widget for the Inspector and the
  ScriptableObject editor (nicified labels, sliders, enum combo, colour picker, quaternion as Euler, typed lists).
- Missing `.plutoscriptable` fields already load with C# defaults at runtime (verified in the bridge); the editor
  shows and saves them.
- Verified: `PlutoGEScriptBehaviourTests` (metadata for every new type and attribute, value round trip including
  a former name, codec), and the existing managed/scripting/prefab suites (15 tests) pass.

### 2026-11-08 — T2.4

- `Scene::RuntimeClock` (scaled/unscaled delta and elapsed time, frame count) reset at StartRuntime and advanced
  per runtime Update; `Scene::kFixedTimeStep`.
- Optional bridge `RegisterTimeApi(GetRuntimeClock)`; managed `Time` static class.
- `Timers.After/Every/CancelAll` and `ScriptBehaviour.After/Every`; ticked once per frame from the bridge's
  per-frame dispatch; owned timers stop with their script (`ScriptBehaviour.IsDestroyed`), all timers are
  scoped to `SceneManager.RuntimeGeneration`.
- Verified: `PlutoGEScriptBehaviourTests` runs 24 frames (time scale 1 then 0): frame count, scaled/unscaled time,
  After once, Every on interval, Cancel, scaled timers frozen and unscaled timers running at time scale 0.

### 2026-11-08 — T2.5

- `ScriptBehaviour.Enabled` with `OnEnable`/`OnDisable` (after OnCreate / before OnDestroy). The managed bridge
  skips update, collision and animation-event callbacks for disabled scripts; the per-frame UI and timer dispatch
  still runs. Native component enable and entity active state already gated scripts.
- Built-in examples' own `Enabled` fields renamed `Spinning` with `[FormerlySerializedAs("Enabled")]`. Game
  scripts with their own `Enabled` member keep working (the compiler warns that it hides the base member).
- Verified: `PlutoGEScriptBehaviourTests` (self-disable after two updates, re-enable from another script,
  OnEnable/OnDisable counts).

### 2026-11-08 — T2.6

- `Scene::OverlapSphere/OverlapBox/RaycastAll` with `PhysicsQueryOptions` (ignored hierarchy, include/exclude
  tags); C# `Physics.OverlapSphere/OverlapBox/RaycastAll/SphereCast` (SphereCast over the existing
  `SweepSphere`). There is no separate layer system; tags fill that role.
- Collision events carry `CollisionContact` (deepest point, normal towards the receiver, summed impulse);
  `ScriptBehaviour.OnCollisionEnter(Collision)` defaults to the old GameObject form.
- Triggers: `Scene::UpdateTriggers` after each physics step contact-tests every active trigger collider against
  moving bodies in the runtime world (shapes cached per trigger by body signature) and raises
  `OnTriggerEnter/Exit` on both entities.
- New bridge entry points are optional (`RegisterPhysicsQueryApi`, `InvokeOnCollisionContact`, `InvokeOnTrigger`).
- Verified: `PlutoGEScriptBehaviourTests` builds ground, crate, trigger zone and a falling ball: overlap, tag
  filter, box, ray-all and sphere-cast results; ground contact normal and impulse; trigger enter/exit on both sides.
  Existing physics suites (foliage collision, surface response, spline road, navigation, ragdoll, surface
  placement, component registry, scene cache) pass.

### 2026-11-08 — T2.7

- `GameObject : IEquatable<GameObject>` (==, Equals, GetHashCode by entity), `IsAlive` (optional
  `RegisterEntityLifetimeApi` -> `Scene::IsEntityAlive`, which treats pending destroys, including an ancestor's,
  as dead), `WorldRotationQuaternion`, `GetComponentInParent/InChildren/GetComponentsInChildren`.
  `ScriptBehaviour.GameObject` is public.
- Verified: `PlutoGEScriptBehaviourTests` GameObjectProbe; managed suites (11) pass.

### 2026-11-08 — T2.8

- `Scene::PlayOneShotSound/StopOneShotSound` (named to avoid the Win32 `PlaySound` macro): transient emitter states
  added to the scene's audio update, kept while the audio system reports the voice active, occlusion measured
  once at start, cleared on StopRuntime. Keys use the top bit to stay apart from emitter keys.
- C# `Audio.PlayAt` / `Audio.Play2D` returning `SoundHandle` (optional `RegisterSoundApi`). Mixer buses are not
  part of this item.
- Verified: `PlutoGEScriptBehaviourTests` writes a WAV into a temp project: PlayAt starts, Stop removes, an empty
  reference is rejected, nothing survives StopRuntime.

### 2026-11-08 — T2.9

- `InputBindingKind.MouseScroll` (managed and editor document/UI), `GetAxisRaw` (unclamped), `GetVector2`
  (length-limited), `WasReleased` (gamepad buttons via per-map held state), `Input.GetAxisRaw/GetVector2/
  GetActionReleased`.
- Every `InputActionMap` registers itself; the bridge advances live maps once per frame (unscaled time) before
  scripts update. `Update` is idempotent per frame.
- Input Mapping editor: **Copy C# constants** (`BuildActionConstantsSource`). A build-time source generator was
  not added: the copied class gives the same compile-time checking without changing the script project.
- Verified: `PlutoGEInputMappingEditorTests` (constants, MouseScroll round trip), `PlutoGEMapBakeTests`, and
  `PlutoGEScriptBehaviourTests` (a hold-trigger map read for 3 frames without calling Update logs no warning).
  Key/scroll values themselves are not simulated (no input injection yet).

### 2026-11-08 — T2.10

- `Engine::StartRuntime/StopRuntime` -> `ScriptEngine::NotifyPlayStateChanged` -> `IScriptRuntime::OnPlayStateChanged`
  -> optional managed `InvokePlayStateChanged`. Start fires before any OnCreate, stop after every OnDestroy.
- Managed `PlaySession`: resets `[ResetOnPlay]` statics in the game assembly (collections cleared in place,
  others to default, read-only non-collections warned), clears `GamePause.IsPaused` and timers, raises
  `Application.PlayStarted/PlayStopped` (handlers isolated), `Application.IsPlaying`.
- An optional "reload the assembly on every Play" mode was not added: `[ResetOnPlay]` and the events cover the
  sample's 25 static registries without the reload cost.
- Verified: `PlutoGEScriptBehaviourTests` runs two sessions (reset int and list, unmarked static kept, event
  counts across sessions); managed and scripting suites pass.

### 2026-11-08 — T2.11

- `Debug.Log/LogWarning/LogError(message, context, [CallerFilePath], [CallerLineNumber])` and
  `Debug.LogException(exception, context)`; optional bridge `RegisterLogContextApi` ->
  `DispatchScriptLog(severity, message, ScriptLogContext)`; hosts without a context sink get plain messages.
- Editor `OnScriptLog`: Console entries carry the entity and the calling line (or the first project stack frame
  for exceptions).
- ScriptCore generates `PlutoGE.ScriptCore.xml` (CS1591/CS1573 suppressed); debugger attach documented for
  Visual Studio, VS Code and Rider.
- Verified: `PlutoGEScriptBehaviourTests` LogProbe (context entity, caller file/line, LogException severity, trace
  and context); XML file produced.

### 2026-11-08 — T3.1

- Design choice: rather than a new "reference + overrides" scene format, instance roots record a content
  fingerprint of their resolved prefab chain (`Entity::GetPrefabFingerprint`, serialized as a trailing `SYNC=`
  token on the existing `PREFAB` record, which older readers ignore). The existing `UpdateInstance` (which keeps
  overrides and added children) rebuilds outdated instances. Same user-facing result, no format migration.
- `Prefab::GetFingerprint/AdoptFingerprints/FindOutdatedInstances/UpdateOutdatedInstances/SyncInstances`;
  fingerprints come from the prefab cache's existing per-dependency content hashes, so instantiation reads no
  extra files.
- Editor: opening a scene adopts fingerprints for legacy instances (scene stays clean), then updates outdated
  instances as one undoable "Update Prefab Instances" edit with a Console summary.
  **Scene > Update Prefab Instances in All Scenes** updates and saves every scene file.
- Verified: `PlutoGEPrefabVariantTests` (fingerprint recorded, survives save/load, prefab edit detected,
  component added, position override kept, legacy adoption); 17 scene/prefab/history/model-instance suites pass.
- Not done: project validation does not yet flag outdated instances (validation lives in the assets library,
  which cannot resolve prefabs); the all-scenes command covers the pre-build case.

### 2026-11-08 — T3.2 deferred

Moving bake data out of scene text needs the data to stay reachable for cooking, content packs, Content Browser
moves and validation. Sidecar files beside the scene would be invisible to the pruned cook (it follows references
found in scene text and the asset catalog), so a build could silently ship without lighting. The safe design is a
new asset type (for example `.plutobake`, binary, content-addressed) written under `<Scene>.bake/` and referenced
from the property as `project://...`, so the existing reference scan, cook, move and rename paths carry it. That
touches the asset type table, the managed `AssetType` mirror, cooking and the Content Browser, and needs its own
tests; it is left as the next storage task rather than done partially. T3.3's number formatting already shortens
the numeric parts of scene text.

### 2026-11-08 — T3.3

- Stable entity IDs: `UpdateInstance` reuses each rebuilt child's previous scene ID (`CloneEntityTreeIntoScene`
  preferred IDs); any IDs that still change are remapped scene-wide, not only inside the instance (references from
  other entities used to break on Update From Prefab).
- Numbers: `SceneSerializer` writes float/double/vector/colour property values as shortest round-trip text
  (`17.7,1,0.25` instead of `17.700001,1.000000,0.250000`); only all-numeric values are rewritten. Script fields
  already use the shared codec (T2.3). Expect a one-time diff when existing scenes are next saved.
- Project file: `EDITOR_*` lines (editor camera, font, viewport effects) moved to
  `.plutoge-editor/<project>.editor-state`; legacy lines are read once and dropped on the next save; exports
  write none. The `ASSET` list is unchanged: hosts that load a source project without scanning (server, runtime)
  depend on it.
- Verified: `PlutoGEProjectFileTests` (state file round trip, legacy migration) and `PlutoGEPrefabVariantTests`
  (child ID kept across update, `17.7,1,0.25` written).
- Also: `.plutoge-editor/` and `.plutoge-cache/` are now asset-infrastructure paths (projects whose asset
  directory is the project root no longer scan editor state). `AuthoringIntegrationTests` now checks parsed
  compiler errors with source lines instead of the raw "Build FAILED." line (raw build output no longer goes to
  the Console since T1.3).
- Full non-GPU run (169 tests): remaining failures are the three window tests (fail silently in this session's
  environment; no platform code changed) and `PlutoGEMaterialReloadTests` ("lost emissive texture or UV set",
  model import code from a0e9cdf2, untouched here).

### 2026-11-08 — T3.4

- `ProjectScaffold` module: `BuildScriptProjectText` (machine-independent .csproj importing
  `<Name>.engine.props`, `<Name>.custom.props` and `.custom.targets`), `BuildScriptEnginePropsText` (this machine's
  ScriptCore paths), `WriteNewProjectFiles` (.gitignore, .gitattributes with LFS rules and `*.plutometa text -filter`,
  `.vscode/launch.json`; never overwrites). `EnsureProjectScriptBuildScaffold` uses it; project creation writes the
  VCS files.
- Verified: `PlutoGEEditorWorkflowTests` (no machine path in the csproj, props content, existing .gitignore kept)
  and `PlutoGEAuthoringIntegrationTests` (a real `dotnet build` of the regenerated project, automatic reload and
  failure handling) pass.

### 2026-11-08 — T3.5

- Project Settings > Build > Profile: Development (debug scripts, script SDK) or Shipping (Release scripts, no SDK);
  `BUILD_PROFILE` in the manifest. `BuildProjectScripts(configuration)`.
- The game build location is remembered (`BUILD_OUTPUT`, project-relative) and used as the next suggestion.
- Every failed build logs "Build failed: ..." to the Console (source Build) through one scope guard instead of only
  setting the status bar; success logs the profile used.
- Not done: product version/icon fields (no consumer yet; an exe icon needs a resource-editing step) and validation
  in the command-line `--export` path (it needs the editor's catalog and model-instance verification setup, which
  would otherwise reject linked-model projects). The editor's build already validates.
- Verified: `PlutoGEProjectFileTests` (profile/output round trip); `PlutoGEEditorUI` builds.

### 2026-11-08 — T3.6

- `AssetHotReload` (`ClassifyHotReloadPath`, `AssetFileWatch` with a quiet period, IO-free observe) polled from
  `EditorShell::PollAssetHotReload` on a worker every second; `ReloadChangedAssets` routes textures and materials
  to `AssetManager::RefreshImportedAssets` (+ `Scene::RefreshMaterialAsset`) and sounds to the new
  `AudioSystem::ForgetClip` (stops voices, frees backend buffers, next play decodes the new file). Works in Play.
- Still polling rather than OS notifications: the scan is a worker-thread directory walk once a second, matching
  the existing script and model watchers; a native watcher backend remains an optimisation.
- Verified: `PlutoGEEditorWorkflowTests` (classification, quiet period, single report, deletions ignored); audio
  suites pass; `PlutoGEEditorUI` builds.

### 2026-11-08 — T4.1

- `EditorCommands` (registry: definitions with id, label, category, default and fixed alternate shortcut, scope,
  repeat; per-frame `Bind` so handlers use that frame's state; `DispatchShortcuts` with a scope predicate;
  `Queue`/`RunQueued` for the palette; overrides stored as `keys.<id>` in `EditorSettings`; `FormatShortcut`,
  `CapturePressedShortcut`, `FuzzyMatchScore`).
- `EditorShellCommands.cpp` registers the shell's commands and replaces `HandleEditorShortcuts` and the File-menu
  shortcut block. Scopes keep the old rules (Global: no text input or popup; Editor: also not in key-owning asset
  editors; Edit: also not in the Content Browser; Scene: `IsSceneShortcutContext`). Menus show the current binding.
- Ctrl+P palette (Help > Command Palette): available commands plus quick-open of project assets via the new
  `EditorShell::OpenProjectAsset` (moved from the Content Browser); assets with no editor are revealed instead.
  Every panel gets a "Window: Show <panel>" command.
- Preferences > Keyboard (also Help > Keyboard Shortcuts): filter, record by clicking and pressing keys,
  conflict warning, remove, reset. Recording suppresses shortcuts so recording Ctrl+S does not save.
- Viewport-local keys (W/E/R, F) and asset-editor keys stay with their panels; they could register commands later.
- Verified: `PlutoGEEditorWorkflowTests` (rebinding, persistence, conflicts, availability, queueing, chord text,
  fuzzy ranking); `PlutoGEEditorUI` builds; `PlutoGEAuthoringIntegrationTests` (links the shell) passes.

### 2026-11-08 — T4.2

- `EditorCameraNavigation` (pure): `OrbitEditorCamera` turns the camera rigidly about a pivot (pitch-limited),
  `IsUsableOrbitPivot`, `DollyEditorCamera` (15% of the pivot distance per notch; carries the pivot forward past
  0.5 m and speeds up again when backing out).
- `UpdateEditorCamera`: Alt + left drag orbits `focusPoint`, re-picking it with `GetSpawnPointInView` when it is
  behind, off to the side or very far; orbit leaves orthographic like looking does. The wheel dollies in
  perspective while not flying. Preference `viewport.altDragOrbits` (Behaviour page) restores Alt-drag look.
- Fly-speed toast: `speedToastSeconds` on the camera, drawn by the Editor Viewport with a fade.
- `ViewportToolPreferences`: tool, space, snap, increments and grid persisted in `EditorSettings`; Ctrl inverts
  snapping during gizmo drags (`IsSnapActive`), for entity, multi-selection, foliage and the other gizmos.
- Verified: `PlutoGEEditorWorkflowTests` (orbit geometry, pivot checks, dolly behaviour, settings round trip and
  sanitising, snap inversion); viewport tests and `PlutoGEAuthoringIntegrationTests` pass.

### 2026-11-08 — T4.3

- `ComponentActions` (editor/ui): `CopyComponentValues`, `PasteComponentValues` and `ResetComponentValues` return
  the names of changed properties (diffed through Serialize), so the Inspector records prefab overrides the same
  way as edits; `AddComponentFromValues`; `GetComponentSlot`.
- `Entity::MoveComponentWithinType`. Components are positional within their type bucket everywhere (serializer,
  undo capture, play-mode changes), so reordering is offered within a type only; cross-type order is not a concept
  in the engine and adding it would change the scene format and undo capture for little gain.
- Inspector header menu: Copy Values, Paste Values, Paste as New Component, Reset, Move Up/Down, Remove. Actions
  run after the component loop as one `ExecuteSceneEdit` each.
- Search matches property names (raw and nicified); a component found that way opens for the search without
  changing its remembered state and lists only matching generic properties. Custom inspectors draw their own
  controls and are shown whole.
- Verified: new `PlutoGEComponentActionsTests` (paste, refusal across types, reset, scripts excluded, paste as
  new, reorder bounds and revision, order saved and loaded); scene history, edit history, prefab variant, scene
  loading, hierarchy and authoring integration suites pass.

### 2026-11-08 — T4.4

- `docs/guide/first-game.md` rewritten around the Third-person template: create, play, tune with Inspector search,
  insert gameplay kits, write a `Coin` trigger script with `[Range]`/`[Tooltip]`, prefab it, build and run. The
  previous from-scratch walkthrough is kept as "From an empty project". Claims checked against the code (template
  contents, menu names, Save As Prefab, Error Pause, startup scene and build profile in Project Settings).
- 71 engineering notes (plans, roadmaps, performance passes, investigations, handoffs, this plan) moved to
  `docs/dev/` with `git mv`; every Markdown link and `docs/...` mention in the repository (including source
  comments) was rewritten, and a link check over all tracked Markdown finds no broken links. `docs/dev/README.md`
  indexes them by area.
- `docs/README.md` now links every user guide: added Faster authoring workflows, Content browser, Scene hierarchy,
  Animation graphs, Skeletal IK, Audio output volume, Controller vibration, Headless servers, Scene loading,
  Graphics presets and TAA; the showcase-scene plan moved out of the user table.

All tiers are complete except T3.2 (`[-]`, design recorded above).
