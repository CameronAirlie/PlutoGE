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
- [ ] **T1.4 Panel-scoped hotkeys.** Viewport W/E/R/F only when the viewport is hovered/focused; hierarchy Ctrl+G only
  when the hierarchy is focused; Backspace no longer deletes entities on Windows; Esc only clears selection from
  scene panels.
- [ ] **T1.5 Asset editor documents.** An `IEditorDocument` interface (title, dirty, save, revert) implemented by the
  asset editors; the shell asks all dirty documents before closing / switching project; editors prompt before
  discarding edits when switching asset; Ctrl+S saves the focused document. Save / Don't Save / Cancel modal.
- [ ] **T1.6 Small editor fixes.**
  - New root entities spawn in front of the editor camera.
  - Viewport drag-drop places on release (Shift keeps stamping).
  - Frame Selected uses combined world bounds of the selection and descendants.
  - Copy/paste supports multi-selection; duplicates get numbered names ("Crate (1)").
  - Game view takes focus when Play starts (preference).
  - Recovery window only after an unclean exit.

## Tier 2 — C# scripting API

- [ ] **T2.1 Two-phase start.** Create every script instance and apply fields first, then call `OnCreate` on each, so
  sibling lookups in `OnCreate` succeed. Applies to runtime start and to instantiated prefabs.
- [ ] **T2.2 Instantiate with configuration.** `Prefab.Instantiate<T>(position, rotation, parent, configure)` where
  `configure` runs before `OnCreate`; Quaternion and parent overloads.
- [ ] **T2.3 Serialized field types and attributes.** Enums, Quaternion, Color, primitive arrays/lists, warning on
  unsupported members, `[Range]`, `[Tooltip]`, `[Header]`, `[FormerlySerializedAs]`. Existing `.plutoscriptable` files
  pick up new fields with their C# default values.
- [ ] **T2.4 Time and timers.** Static `Time` (deltaTime, unscaledDeltaTime, time, unscaledTime, frameCount,
  timeScale) and `Timers.After/Every` bound to the owning behaviour's lifetime.
- [ ] **T2.5 Behaviour enable/disable.** `ScriptBehaviour.Enabled`, `OnEnable/OnDisable`, callbacks skipped while
  disabled.
- [ ] **T2.6 Physics queries and triggers.** Overlap sphere/box, SphereCast, RaycastAll, layer masks, trigger callbacks,
  contact data on collisions.
- [ ] **T2.7 GameObject ergonomics.** Value equality, `IsAlive`, public `GameObject` on behaviours, quaternion world
  rotation, `GetComponentInParent/InChildren`.
- [ ] **T2.8 Fire-and-forget audio.** `Audio.PlayAt / PlayOneShot` on an engine voice pool.
- [ ] **T2.9 Input action gaps.** Unclamped delta axes, scroll binding, `WasReleased`, Vector2 composites, engine-driven
  map updates.
- [ ] **T2.10 Play session hygiene.** `Application.PlayStarted/PlayStopped` events, engine statics reset on stop,
  `[ResetOnPlay]` for user statics.
- [ ] **T2.11 Debugging.** `Debug.LogException(ex, context)`, context entity on logs (click to select), documented
  debugger attach, ScriptCore XML docs for IntelliSense.

## Tier 3 — Project data and pipeline

- [ ] **T3.1 Prefab instances by reference.** Store instances as prefab reference + overrides and resolve on load so
  unopened scenes pick up prefab changes.
- [ ] **T3.2 Bake data out of scene text.** Baked lighting / captures written to binary side files, scene stores a
  reference; legacy inline data still loads.
- [ ] **T3.3 Diff-friendly scene and project files.** Stable entity IDs, shortest round-trip float formatting, editor
  state moved out of `.plutoproject` into per-user editor storage.
- [ ] **T3.4 VCS-ready new projects.** Generate `.gitignore` / `.gitattributes`; csproj references the engine through
  a property, keeps a user-owned import for customisation.
- [ ] **T3.5 Build settings and profiles.** Development / Shipping profiles, Release scripts for shipping, remembered
  output folder, product name / version / icon, every failure logged to the Console.
- [ ] **T3.6 Hot reload beyond models.** File watching for textures, materials and audio with live reload.

## Tier 4 — Comfort and discoverability

- [ ] **T4.1 Command registry, shortcuts list, command palette.** One registry for editor commands with shortcut,
  rebinding in Preferences, Help > Keyboard Shortcuts, Ctrl+P palette with quick-open.
- [ ] **T4.2 Viewport navigation.** Alt+LMB orbit around the selection, wheel dolly in perspective, hold-Ctrl snap,
  speed toast, persisted gizmo/snap settings.
- [ ] **T4.3 Inspector component actions.** Copy / Paste values, Reset, Move Up / Down, property search.
- [ ] **T4.4 Docs reorganisation.** First-game guide rewritten around templates; engineering notes moved to
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

