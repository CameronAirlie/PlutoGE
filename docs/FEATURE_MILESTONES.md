# PlutoGE feature milestones

This is the implementation backlog agreed on 2026-09-07. Work proceeds in the
order below, with each milestone delivered as a usable vertical slice. A milestone
is complete only when its acceptance criteria and relevant validation pass.
Record partial deliveries explicitly; existing systems are foundations, not proof
that a milestone is complete.

## Engineering rules

- Keep editor workflows in EditorUI, reusable gameplay behavior in the engine,
  and managed wrappers thin. Reuse component serialization, asset references,
  scene transactions, and the rendering abstraction.
- Keep changes compatible with existing projects and both graphics backends.
- Make scene edits undoable. Validate references and ownership across scene
  replacement, script reload, and play/stop transitions.
- Separate deterministic data/logic from UI and device access so important
  behavior can be tested without launching the editor.
- Avoid speculative frameworks. Add shared abstractions when concrete consumers
  need them; give expensive operations explicit cancellation/lifetime rules.
- Build affected targets, run focused regression checks, document limitations,
  and perform interactive checks when the environment permits them.

## Milestones

| ID | Feature | Scope | Status |
| --- | --- | --- | --- |
| M01 | Keep selected play-mode changes | Small | Implemented; automated checks passed |
| M02 | Named viewport bookmarks | Small | Implemented; automated checks passed |
| M03 | Drop selection onto ground | Small | Implemented; automated checks passed |
| M04 | Find asset references | Small–medium | Implemented; automated checks passed |
| M05 | Autosave and recovery | Small–medium | Implemented; automated checks passed |
| M06 | Project validation panel | Medium | Implemented; automated checks passed |
| M07 | Gameplay debug drawing | Medium | Implemented for editor views; automated checks passed |
| M08 | Camera rigs | Medium | Implemented; automated checks passed |
| M09 | Surface response assets | Medium | Planned |
| M10 | Prefab variants | Medium–large | Planned |
| M11 | Sequencer/timeline | Large | Planned |
| M12 | Additive scene loading and streaming | Large | Planned |
| M13 | Networked entity replication | Large | Planned |
| M14 | Procedural spline roads | Large | Planned |

### M01 — Keep selected play-mode changes

- Explicit stop-and-review action lists changed entity/component properties with
  before/after values; retain only checked values, with nothing selected by default.
- Restore the original scene first and apply retained values as one undoable edit.
- Preserve ordinary Stop behavior and pre-play dirty state.
- Handle scene transitions, deleted/spawned entities, changed component layouts,
  invalid runtime references, and cancel safely. Structural changes and writes to
  shared assets are outside this milestone.
- Validate selective application, cancellation, prefab overrides, and undo/redo.

### M02 — Named viewport bookmarks

- Save, recall, rename, replace, and delete named camera views per project.
- Retain position, orientation, projection, and orthographic size across reopen.
- Keep bookmark data separate from runtime scenes and handle malformed data safely.

### M03 — Drop selection onto ground

- Place the selected object onto terrain/collidable geometry in one undoable edit.
- Exclude the selected hierarchy from hits; handle parents and no-hit cases.
- Offer normal alignment and optional bounded random yaw/scale.

### M04 — Find asset references

- Query incoming references from the content browser and navigate to their owners.
- Cover scenes, prefabs, materials, and other registered serialized asset types.
- Report scan errors; invalidate cached results after edits/imports/moves/deletes.

Implemented with a shared read-only extractor and incremental index, separate
from metadata generation. A cancellable background search lists owners and scan
issues, supports navigation, and refreshes after saved file changes. Native
format tests cover spaces, relative material paths, binary strings, and files
larger than 16 MiB. See [Editor workflows](EDITOR_WORKFLOWS.md#find-asset-references)
for format coverage and the limits of literal reference scanning.

### M05 — Autosave and recovery

- Configurable rotating scene backups, including unsaved scenes, isolated by project.
- Recover through a reviewable editor workflow after restart; never overwrite the
  user's scene silently. Avoid saving runtime simulation as authoring data.
- Test interrupted writes, retention, and recovery of malformed/missing backups.

### M06 — Project validation panel

- Reusable structured diagnostics for missing scripts/assets/cameras and invalid
  colliders, with severity, owning entity/asset, and navigation.
- Run on demand and before export; keep validation independent from ImGui.

### M07 — Gameplay debug drawing

- C# lines, spheres, and labels, durations, named categories, and visibility controls.
- Define frame/time/pause behavior and clean up across scene/play transitions.
- Render through the existing abstraction on OpenGL and Vulkan, with bounded storage.

### M08 — Camera rigs

- Follow/orbit, smoothing, collision avoidance, shake, and blended transitions.
- Serializable settings, managed control, and examples using vehicles and characters.
- Stable behavior across frame rates, missing targets, pause, and scene changes.

Implemented as an engine Camera Rig component with a late runtime update, sphere
sweep obstruction avoidance, per-scene target IDs, exponential smoothing,
world-space follow/orbit poses, fading positional shake and smooth target blends.
The inspector uses existing serialization/history; prefab copies remap targets.
C# controls and character/vehicle examples are included. See
[Camera rigs](CAMERA_RIGS.md) for setup, control semantics and scope.

### M09 — Surface response assets

- Reusable surface assets connect colliders/terrain to footsteps and impacts.
- Resolve sound, particles, decals, and friction with defaults for unknown surfaces.
- Include managed access, authoring, cooking, and a working example.

### M10 — Prefab variants

- Derived assets inherit their base and retain explicit overrides.
- Editor create/apply/revert workflows, nested references, and base-change propagation.
- Detect cycles/missing bases and preserve existing prefab behavior.

### M11 — Sequencer/timeline

- Author tracks for cameras, transforms, audio, lighting, and script events.
- Preview/scrub, play/stop, binding repair, serialization, and runtime playback.
- Define interpolation, event boundaries, looping, and restoration after preview.

### M12 — Additive scene loading and streaming

- Persistent gameplay scene plus asynchronous load/unload of level sections.
- Explicit scene ownership, activation, cancellation, progress, and failure handling.
- Keep entity references, physics, navigation, audio, rendering, and scripts correct
  through unload; GPU work must remain on the appropriate owning thread.

### M13 — Networked entity replication

- Versioned entity spawn/despawn, authority/ownership, replicated properties, and
  transform interpolation layered above transport.
- Bounded queues/payloads, late join, disconnect cleanup, and scene lifetime handling.
- Validate with a small cooperative sample and reproducible multi-client tests.

### M14 — Procedural spline roads

- Spline road meshes with banking, junctions, guardrails, and roadside placement.
- Terrain conformity, collision, materials, LODs, and asset/export integration.
- Deterministic rebuilds, undoable edits, and local rebuilds for changed sections.

## Delivery log

- 2026-09-07: Recorded all fourteen proposals, implementation order, boundaries,
  and acceptance criteria. Started M01 against the existing play snapshot and
  scene-history systems.
- 2026-09-07: Implemented M01 value snapshots, review UI, selective staged
  application, prefab override paths, and isolated play history. Added regression
  checks for selection, snapshot restoration, conflicts, structural changes, and
  invalid references. Editor build and focused CTest suite passed on GCC/Windows.
- 2026-09-07: Implemented M02 versioned editor sidecars and bookmark management UI.
  Standalone persistence tests pass, including exact float round trips, atomic
  replacement, duplicate names, invalid camera values, malformed input, and CRLF.
  Editor build and focused CTest suite passed on GCC/Windows. Usage and limitations
  are documented in [Editor workflows](EDITOR_WORKFLOWS.md).
- 2026-09-07: Implemented M03 placement planning, hierarchy-aware raycasts, static
  mesh/primitive support bounds, parent-space conversion, normal alignment, and
  reproducible randomization. Added an explicit physics-query synchronization API
  for authoring operations between scene updates. Editor build and focused CTest
  suite passed on GCC/Windows.
- 2026-09-07: Final combined build succeeded for the editor and all three new test
  targets. All three CTest suites passed (21.19 seconds); `git diff --check` passed.
  At that point M04–M14 remained planned.
- 2026-09-07: Implemented M04 read-only extraction, incremental background lookup,
  content-browser navigation, error reporting, and shared cooker dependencies.
  Editor and both new test targets built on GCC/Windows. All five focused suites
  passed (5.41 seconds), covering the new scanner/cooker and M01–M03 regressions.
  `git diff --check` passed. At that point M05–M14 remained planned.
- Follow-up found during M04: cooking into an existing destination failed with
  `File exists` on GCC/Windows despite `overwrite_existing`. Fresh-destination
  cooking passes; repeated export overwrite behavior needs separate investigation.
- 2026-09-07: Implemented M05 project-isolated rotating scene backups, configurable
  interval/retention, atomic publication, checksums, and recovery into an unsaved
  scene. Save Project also requests a path for recovered scenes. Runtime/bake/edit
  guards protect authoring backups. Storage tests cover settings, rotation,
  interrupted writes, corruption, missing files, and project isolation.
- 2026-09-07: Extended undo/redo to dirty-marked serialized inspector and scene
  edits, grouped by interaction. Fixed Ctrl+Shift+Z double execution, failed
  restores losing entries, project saves clearing history, stale savepoints after
  project loads, and prefab refresh overwriting restored snapshots. Added strict
  snapshot loading and selection restoration by ID. History tests cover failed
  transfers, malformed snapshots, component/name/transform roundtrips, and commands.
  Final GCC/Windows editor build passed; all seven focused suites passed (29.76
  seconds). `git diff --check` passed. Interactive checks remain pending.
  At that point M06–M14 remained planned.
- 2026-09-07: Implemented M06 reusable read-only validation in Assets, with stable
  diagnostic codes, severities, owner references, entity IDs, and source lines.
  Added an editor panel with warning filtering and entity/content-browser
  navigation; shared asset reveal logic with M04. Saved-data validation now gates
  editor export after script builds. On-demand checks substitute the current
  serialized scene for its saved copy without loading scenes or running scripts.
  Regression checks cover asset/script references, unavailable class catalogues,
  active camera hierarchies, invalid colliders including float overflow/underflow,
  malformed records, cyclic hierarchies, current-scene substitution and read-only
  behavior. Final GCC/Windows editor build and all eight focused suites passed
  (6.77 seconds); `git diff --check` passed. Interactive UI/export and Linux checks
  remain pending. At that point M07–M14 remained planned.
- 2026-09-07: Implemented M07 C# line, wire-sphere and label submissions, with a
  value-owned, mutex-protected bounded store, simulation-time expiry, pause
  behavior, category visibility, and transition/reload cleanup. Scene and Game
  view overlays reuse clipped world-space helpers and the OpenGL/Vulkan ImGui
  compositor. Standalone-player visualization is not included in this editor
  milestone. Native tests cover lifetime, filtering, bounds, invalid data, reset,
  and concurrent snapshots; managed smoke tests exercise ABI registration, packet
  layout, callback dispatch, and UTF-8 boundaries. All ten focused tests passed
  (9.57 seconds); `git diff --check` passed. Interactive backend checks remain pending.
  M08 camera rigs is next; M08–M14 remain planned.
- M07 build note: direct GNU ld links exhausted memory, including with
  `--no-keep-memory`, `--reduce-memory-overheads`, and `--strip-debug`. The editor
  linked successfully using temporary copies of the current archives stripped
  with `objcopy --strip-debug` under `out/m07-stripped`; original build archives
  remain intact. The subsequent normal editor target check passed. This executable
  omits debug symbols. The three linker flags remain in the local GCC CMake cache;
  no repository build defaults were changed.

- 2026-09-07: Implemented M08 engine camera rigs with follow/orbit poses,
  exponential smoothing, sphere-sweep obstruction avoidance, fading positional
  shake, and interruptible target blends. Rigs run after script LateUpdate and
  physics presentation, freeze with simulation time, and reset on runtime
  transitions. Added inspector creation/editing, snapshot persistence, prefab
  cloning/remapping, C# controls, and character/vehicle example scripts.
  Native tests cover pose math, parent transforms, collision/hierarchy filtering,
  pause/restart, failed commands, serialization, undo/redo and prefab duplication;
  managed tests cover ABI layout, registration and command dispatch. All thirteen
  focused suites passed (76.87 seconds), including the earlier mesh restoration
  regression. The final GCC editor and managed SDK build passed. Memory limits
  required temporary stripped archives and a smaller GNU linker hash table;
  original archives remain intact, and the executable omits debug symbols.
  Interactive OpenGL/Vulkan checks remain pending. M09-M14 remain planned.

Reproduce the focused checks from the repository root:

```powershell
cmake --build out/build/gcc --target PlutoGEEditor PlutoGECameraRigTests PlutoGEMultiEntityEditTests PlutoGEDebugDrawTests PlutoGEProjectValidationTests PlutoGESceneRecoveryTests PlutoGESceneHistoryTests PlutoGEAssetReferencesTests PlutoGEAssetReferenceCookingTests PlutoGEPlayModeChangesTests PlutoGEViewportBookmarksTests PlutoGEGroundPlacementTests -j 1
ctest --test-dir out/build/gcc -R '^PlutoGE(DebugDraw|DebugDrawManaged|ProjectValidation|SceneRecovery|SceneHistory|AssetReferences|AssetReferenceCooking|PlayModeChanges|ViewportBookmarks|GroundPlacement|MultiEntityEdit|CameraRig|CameraRigManaged)Tests$' --output-on-failure
```

The build uses one compile job because the first parallel GCC build exhausted
available memory. Its empty `Entity.cpp.obj` was removed and rebuilt successfully.

Interactive editor verification on OpenGL/Vulkan and a Linux build remain pending
for this delivery; automated data/scene checks do not substitute for those checks.

### Additional editor workflow � Multi-entity editing

Implemented shared hierarchy/viewport selection, Ctrl toggling, visible Shift
ranges, group transform gizmos, mixed local values and common component properties,
and selection-wide duplicate/delete. See [controls and verification](MULTI_ENTITY_EDITING.md).
Automated regression coverage is in `PlutoGEMultiEntityEditTests`; interactive
OpenGL/Vulkan verification remains pending.
