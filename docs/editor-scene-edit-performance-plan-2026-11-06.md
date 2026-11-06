# Editor Scene Edit Performance: Fix Plan

Nov 6, 2026

## Status

Phases 0 to 4 are implemented, on top of the foliage editor work, which landed first. The full suite passes except `PlutoGEOpenGLSsrTests` and `PlutoGEVulkanDdgiTests`, which fail with their known baseline messages. Two items are still open, both because they need someone in the running editor: the in-editor frame times in Phase 0, and the manual pass under "Tests to run". Hierarchy clipping, viewport icons and in-place undo have been compiled and their logic tested, but not yet exercised by hand; the editor has only been launched on a project copy, to confirm it starts.

Undo now lives in `SceneEditHistory`, which runs without an editor, so tests can count its saves. `docs/EDITOR_AUTHORING.md` describes how to record an edit.

### Results

Measured in Release without the editor or GPU, by `PlutoGESceneEditScalingTests`. Real scenes loaded with full project context, from copies. No file in the copies changed, so a project-context load does not appear to write. The CoD copy had no `Library`, which did not stop `Standoff` keeping its data.

| Scene | Entities | Snapshot size | First save | Later saves | Undo by snapshot (load) |
| --- | --- | --- | --- | --- | --- |
| `Verdant` | 88 | 56.6 MB | 270 ms | 14 ms | 627 ms |
| `Basin` | 5,485 | 41.5 MB | 112 ms | 41 ms | 335 ms |
| `Standoff` | 556 | 26.1 MB | 72 ms | 10 ms | 221 ms |

| Synthetic | Save | Snapshot edit | Snapshot undo | Scoped edit | Scoped undo | Structural edit | Structural undo |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1,000 entities | 9 ms | 10 ms | 9 ms | 0.005 ms | 0.008 ms | 0.17 ms | 0.003 ms |
| 5,000 entities | 47 ms | 48 ms | 49 ms | 0.006 ms | 0.008 ms | 1.0 ms | 0.016 ms |
| 20,000 entities | 190 ms | 196 ms | 262 ms | 0.006 ms | 0.008 ms | 6.1 ms | 0.043 ms |

The synthetic scene is not the one in the Evidence section, whose 1,000 entities produced 2.6 MB rather than 0.55 MB, so compare these with each other and not with that table. Real undo also pays for the scene swap, which these do not measure. A snapshot edit includes one save, so it is now close to the cost of a single save rather than three.

Against the targets below:

| Measure | Target | Result |
| --- | --- | --- |
| Saves for an `ExecuteSceneEdit` | 1, then 0 for converted edits | 1 for snapshot edits, 0 for scoped ones (asserted) |
| Add component in `Verdant` | No serialization of foliage | 0 saves when scoped |
| Add component with 5,000 entities | Not dependent on entity count | About 1 ms, from a hierarchy walk that grows with entity count but stays far below a save |
| Undo with 5,000 entities | Not dependent on entity count | 0.008 ms scoped; 49 ms plus the scene swap by snapshot |
| Undo depth in `Standoff` | At least 8 steps | About 9, calculated from 26 MB per step against 256 MB, not measured in the editor |

### Answers to the open questions

- **`Save(Load(x)) == x` holds** on `Verdant`, `Basin` and `Standoff`, after one bug fix: script components wrote undeclared fields in `unordered_map` order, which changed between loads. They now write them sorted, so those scenes produce a small one-time diff. A new terrain's blank material reference also settles on its first load, so the very first round trip of a freshly created terrain differs.
- **Blob writers** were few enough to cover. Foliage already went through `MarkInstancesDirty`, and the others have a handful of writes each. `SceneEditHistoryTests` checks the cached text against a fresh encode after each foliage write.
- **Encoders profiled in 1.4.** Base64 took 25 ms for 33.6 MB, so it stays as it is. Terrain heights took 332 ms for 11.4 MB, about 13 times slower per byte than foliage, so they moved to `to_chars` as well. Their reader took 59 ms and stays as it is. Terrain text changes on the next save, with the same exact values.

### Where this differs from the tasks below

- **1.3 and 3.3, before-states.** An edit reported after it happened cannot capture what came before it, which the plan did not account for. Scoped edits solve this with a cheap baseline of the selection's light properties, taken when the selection changes rather than when an item activates. For an edit of unknown reach arriving while the whole-scene state is behind, nothing can recover its before-state, so it merges with the steps since that state into one undo step. Undo gets coarser but nothing is lost. To keep this rare, the editor refreshes the whole-scene state after 2 seconds without an edit or interaction, which is a save, though not one caused by an edit.
- **2.3.** Blobs are never diffed from scoped baselines. An edit that changes one records as a snapshot, which is cheap once its text is cached.
- **3.2.** Gizmo drags use the same selection baseline as the inspector, rather than a separate transform capture, so prefab overrides and every other field are covered without extra code.
- **3.4.** Converted: create, duplicate, paste, delete, reparent, reorder, group, rename, activate, set pivots, instantiate prefab, add and remove component. An edit that turns out to touch something outside its declared scope falls back to a snapshot. Skeleton attachment edits, prefab variants and scene-wide tools stay as snapshots.
- **Terrain painting** stays a snapshot gesture, because it changes heavy data.
- **`MarkSceneClean` keeps a capture**, against 2.2. Project templates build scenes without reporting edits, and a stale before-state made a failed edit roll back to a scene missing those entities. Saving already serialized the scene, so the extra capture is small.

### Accepted risk

Task 1.1's risk turned out to be real. A change nobody reports joins the next snapshot step, so undoing that step reverts it too, and the same goes for rolling back a failed edit. `TestUnreportedChangeJoinsTheNextStep` pins this behaviour. `AuthoringIntegrationTests` hit it by running the runtime directly rather than through Play, and now calls `Resynchronize` to say so. Window > Tools > Verify Scene Undo finds edits like this, along with scoped edits that reach further than they declared. Only the inspector's sites have been audited by reading, so it is worth turning on during the manual pass, particularly for mesh material edits, whose in-place restore no test covers because it needs real assets.

## Summary

Scene edits stall because undo history re-serializes the whole scene to text, up to three times per edit, on the UI thread. This plan removes that cost in stages, and the end state makes edit and undo cost independent of scene size.

- **Root cause:** a snapshot costs about 135 µs per entity on the test scene, and scenes with large blobs pay far more. `Verdant` has only 88 entities but an estimated save time of at least 3.5 s.
- **Fastest gain:** Phase 1 takes a 3-save edit in `Verdant` from at least 10 s to an estimated 0.5–1 s, in about 1–2 days.
- **Real fix:** Phase 3 records only what changed and applies undo in place, so it no longer reloads the scene.
- **Order:** Phases 0 and 1 first. Phases 2 and 3 are worth doing separately, and each one stands on its own if work stops there.

| Phase | Delivers | Estimate | Gate |
| --- | --- | --- | --- |
| 0: Baseline | Profiler scopes, scaling test | About half a day | Baseline set |
| 1: Quick wins | One save per edit, faster encoders | About 1–2 days | 1 save per edit |
| 2: Sharing | Shared snapshots, cached blobs | A few days | 2x undo steps |
| 3: Scoped undo | Record only changes, undo in place | About 1–2 weeks | No edit saves |
| 4: Remaining UI fixes | Hierarchy clipping, viewport icons, expansion state | About 1–2 days | Independent of the phases above |

Phases 0 to 3 build in order. Phase 3 depends on task 1.3 from Phase 1 and task 2.2 from Phase 2. Phase 4 can land at any time.

## Evidence

Every snapshot goes through `SceneSerializer::SaveToString`, and most edit paths take more than one. Line numbers below are from the working tree on 2026-11-06, which includes uncommitted foliage editor changes.

### Saves per edit

| Action | Full saves | Code |
| --- | --- | --- |
| Gizmo drag or inspector change, on release | 1 | `FlushUntrackedSceneEdit`, `EditorShell.cpp:1840` |
| Add component, create entity, other `ExecuteSceneEdit` calls | 3 | `EditorShell.cpp:1787` |
| `BeginSceneEdit`/`EndSceneEdit` gestures (terrain paint, collider resize) | 3 | `EditorShell.cpp:1901`, `:1923` |
| Foliage and terrain layer commands | 1 | `PushSceneEditCommand`, `EditorShell.cpp:1885` |
| Undo or redo | 1 load + 2 saves, then a scene swap | `RestoreSceneState`, `EditorShell.cpp:1580` |

### Measured cost

| Scene | Snapshot size | 1 save | 3 saves | Undo (load + 2 saves) |
| --- | --- | --- | --- | --- |
| Synthetic, 1,000 entities | 2.6 MB | 142 ms | 425 ms | ~415 ms |
| Synthetic, 5,000 entities | 13 MB | 931 ms | 2.8 s | ~2.7 s |
| Synthetic, 20,000 entities | 52 MB | 2.7 s | 8.1 s | ~8 s |
| `Basin` | 42 MB | 1.2 s | 3.5 s | ~3.7 s |
| `Standoff` (556 entities) | 27 MB | 2.3 s | 6.8 s | ~6.2 s |
| `Verdant` (88 entities), estimated | 56 MB | at least 3.5 s | at least 10 s | not measured |

### What fills the real snapshots

| Scene | Property | Size | Encoding |
| --- | --- | --- | --- |
| `Verdant` | `FoliageComponent` instances | 53 MB | `ostringstream`, 6 significant digits |
| `Verdant` | `TerrainComponent` height samples | 2.9 MB | `SerializeHeightSamples` |
| `Standoff` | `AmbientLightVolumeComponent` baked data | 18 MB | `ostringstream`, `max_digits10` |
| `Standoff` | `IblCaptureComponent` capture pixels | 8.4 MB | Base64 |

### Limits of these numbers

- The benchmark ran in Release without the editor or GPU, so real undo also pays for the scene swap, which is not measured.
- Real scenes loaded without a project context, and some components were dropped. `Standoff` and `Basin` kept nearly all their bytes.
- `Verdant` lost its foliage, so its figure combines a separate encoder test (760k instances in 3.1 s) with the rest of the scene.
- Per-frame costs in the inspector are calculated from measured `Serialize` times, not measured in the editor.

## Phase 0: Instrumentation and baseline

Phase 0 makes the stalls visible in the profiler and fixes a baseline, so every later phase can prove its gain. Estimate: about half a day.

- [x] **Add profiler scopes.** Add `core::CpuScope` to `CaptureSceneState`, `RestoreSceneState` and `FlushUntrackedSceneEdit`. None of these has a scope today.
- [x] **Add a scaling test.** Add `tests/SceneEditScalingTests.cpp`, starting from the temporary `SceneEditBench.cpp`, which is not in the repo and needs recreating if the scratchpad is gone. It builds synthetic scenes and reports save and load times.
- [x] **Assert on ratios.** Compare results against saves per edit and scaling ratios, because absolute milliseconds would make the test flaky across machines.
- [x] **Measure real scenes properly.** Load `Verdant`, `Basin` and `Standoff` with full project context, and replace the estimates in the Evidence section. Work on copies, because a project-context load has not been checked for writes.
- [ ] **Capture in-editor numbers.** Record undo and selection frame times in the profiler, with other editors closed so contention does not inflate them.

**Done when:** the profiler attributes a snapshot stall by name, and the test reports a baseline.

## Phase 1: Quick wins

Phase 1 removes redundant work without changing how undo is designed. Estimate: about 1–2 days.

### 1.1 Remove redundant saves

`ExecuteSceneEdit` and gesture edits drop from 3 saves to 1. File: `editor/ui/src/EditorShell.cpp`.

- [x] In `ExecuteSceneEdit` and `BeginSceneEdit`, use `m_observedSceneState` and `m_observedSceneGenerations` as the "before" state after the existing flush, when `m_untrackedSceneEdit` is false.
- [x] In `ExecuteSceneEdit` and `EndSceneEdit`, copy "after" into `m_observedSceneState` instead of calling `SynchronizeHistoryState`.
- [x] Handle an empty `m_observedSceneState` by falling back to a capture.

**Risk:** a mutation that never called `MarkSceneDirty` would land inside the next undo step. It already goes unrecorded today, so the failure is mild, but `SceneHistoryTests` should cover it.

### 1.2 Avoid double saves on undo and redo

Undo and redo drop from 2 saves to 0 or 1.

- [x] Stop `Undo` and `Redo` calling `SynchronizeHistoryState` when `SetScene` has just done it.
- [x] Test whether `Save(Load(x)) == x` for real scenes. If it holds, set the observed state from the history entry and skip the remaining save.
- [x] Keep the save whenever `PrepareModelInstanceRefresh` replaced the scene, because the result can differ from the entry.

### 1.3 Make the command sync lazy

Foliage and terrain layer commands stop paying for a full save. Files: `EditorShell.cpp`, `foliage/FoliageEditing.cpp`, `TerrainLayerEditing.cpp`.

- [x] Add a `m_observedStateStale` flag, and set it in `PushSceneEditCommand` instead of calling `SynchronizeHistoryState`.
- [x] Capture only when the observed state is next read: flush, before-state reuse, or the dirty check in `Undo` and `Redo`.
- [x] Clear `m_untrackedSceneEdit` correctly, because the apply lambdas call `MarkSceneDirty` during undo and redo.

**Risk:** the first snapshot edit after a command still pays one save, until Phase 3.

### 1.4 Speed up blob encoding

`std::to_chars` took 760k foliage instances from 3,125 ms to 224 ms in the encoder test. Files: `FoliageComponent.cpp`, `AmbientLightVolumeComponent.cpp`, `TerrainComponent.cpp`.

- [x] Rewrite `SerializeInstances` with `std::to_chars` into a reserved string. This also fixes the precision loss, because shortest output round-trips exactly.
- [x] Rewrite `DeserializeInstances` with `std::from_chars` on string views, in place of a `stringstream` and `stof` per record. Old files stay readable because the text format is unchanged.
- [x] Apply the same change to the "Baked Data" encoder and its reader, which also use streams.
- [x] Profile `SerializeHeightSamples` and Base64 before changing them, because neither has been measured alone.

**Trade-off:** exact foliage values make files about 18% larger, and existing scenes produce a large diff on their next save.

### 1.5 Tidy the serializer

File: `engine/scene/src/SceneSerializer.cpp`.

- [x] Remove the `extendedMaterials` and `extendedManagedFields` passes (lines 268–281), which serialize every mesh and script component an extra time just to pick a format version.
- [x] Replace them with cheap queries, such as a `MeshComponent::HasInlineMaterialVersion()` and a matching script check. They must return exactly what the passes returned, because the result selects the format version.
- [x] In `SaveToString` (line 545), avoid the `output.str()` copy. Write into a string reserved from the previous snapshot's size.
- [x] Stop `IsAssetPathProperty` repeating its registry lookup for every property, by resolving the type info once per component.

### 1.6 Stop serializing blobs per frame in the inspector

File: `editor/ui/src/panels/InspectorPanel.cpp`.

- [x] Add a way to serialize without heavy data, such as `Component::SerializeForInspector()`, defaulting to `Serialize()`. Override it for foliage, ambient volumes, IBL captures and terrain.
- [x] Use it at lines 2635 and 3425. `InspectFoliage` clears the list straight after line 2635 builds it, so foliage pays for data it never shows.
- [x] Check every writer that passes inspector properties back to `Deserialize`, so an omitted blob is never read as an empty one. This is the main risk in this task.

### 1.7 Fix multi-select cost

Files: `InspectorPanel.cpp`, `MultiEntityEdit.cpp`.

- [x] At `InspectorPanel.cpp:2282`, serialize each instance once and build a name-to-value map, instead of re-serializing every instance for every property.
- [x] Cache `FindCommonComponents` results, and rebuild them when the selection revision changes or an edit happens.
- [x] Skip the mixed-value work when a component header is closed.

### Expected result

A 3-save edit in `Verdant` should fall from at least 10 s to about 0.5–1 s. That estimate combines the single save from 1.1 with the encoder result from 1.4, and Phase 0 should confirm it.

**Done when:** `SceneHistoryTests`, `MultiEntityEditTests` and the foliage editor tests pass, and the scaling test shows one save per edit.

## Phase 2: Shared snapshots

Phase 2 stops undo history holding duplicate scene text, which roughly doubles undo depth. Estimate: a few days. Each snapshot still costs one save per edit until Phase 3, except for cached blobs.

### 2.1 Share immutable states

Files: `SceneHistory.h`, `EditorShell.h`, `EditorShell.cpp`.

- [x] Change `beforeState`, `afterState`, `m_observedSceneState` and `m_savedSceneState` to `std::shared_ptr<const std::string>`.
- [x] Make each entry's "before" point at the previous "after" whenever they are the same state.
- [x] Count each distinct pointer once in the `PushSceneHistoryEntry` limit. Today `Standoff` gets about 4 undo steps and `Verdant` about 2 under the 256 MB cap.
- [x] Compare pointers first and bytes second, in all state comparisons.

### 2.2 Track dirty state by history position

- [x] Give each history entry a unique ID, and store the ID that was current at save time.
- [x] Replace the string comparisons at `EditorShell.cpp:1991` and `:2019` with a comparison of those IDs.
- [x] Remove the capture in `MarkSceneClean` once nothing else reads `m_savedSceneState`. Check `SceneRecoveryUI.cpp:108` first, which clears it.

### 2.3 Cache blob text per component

This keeps a save fast when the edit did not touch a blob. Files: `Component.h`, the blob components, `SceneSerializer.cpp`.

- [x] Add a content revision to foliage, terrain, ambient volume and IBL components, and bump it in every setter that changes blob data.
- [x] Keep the encoded text beside its revision, and reuse it when the revision has not changed.
- [x] Let the serializer take a blob by reference, so it does not copy it through `Property::value`. This needs a new path beside `Serialize()`, because `Property` holds a `std::string`.

**Risk:** a missed revision bump saves stale data, which is the worst failure in this plan. Mitigate by keeping blob writes behind few setters, and compare the cached output against a fresh encode in debug builds.

**Done when:** repeated small edits in `Verdant` do not re-encode foliage, and the undo limit allows at least twice as many steps in `Standoff`.

## Phase 3: Scoped undo

Phase 3 is the real fix: an edit records only what changed, and undo applies it in place. Edit and undo costs stop depending on scene size, and no scene reload means no GPU republish. Estimate: 1–2 weeks.

`SceneHistoryEntry` already has `undo` and `redo` slots, and foliage and terrain layers already use them, so this extends a working pattern.

### 3.1 Prepare the history

- [x] Finish 1.3 and 2.2 first, so commands need neither a full save nor a string comparison.
- [x] Add an `EntityStateCapture` helper that captures one entity's transform, active state, name, tags, prefab data and component properties, and restores them in place.
- [x] Restore through existing setters where possible, so render, physics and shadow invalidation still fire.

### 3.2 Convert transforms

This is the first conversion because it is the most common edit and has the smallest state. File: `ViewportPanel.cpp`, around lines 2861 and 3819.

- [x] Record the transforms of selected entities when the gizmo drag starts.
- [x] On release, push one command holding before and after transforms, and affected prefab override changes.
- [x] Apply the same pattern to the transform fields in the inspector.

### 3.3 Convert inspector properties

File: `InspectorPanel.cpp`, which has about 75 edit call sites, so this should not touch each one.

- [x] Capture the selected entities' state when an inspector item activates or the frame's first edit occurs.
- [x] Diff against the captured state on flush, and push a command holding only what changed.
- [x] Exclude blobs from the diff unless the owning component's content revision changed (from 2.3).
- [x] Fall back to a full snapshot when a change reaches beyond the selection, such as prefab or model instance edits.

### 3.4 Convert structural edits

Files: `SceneHierarchyPanel.cpp`, `EditorShell.cpp`.

- [x] Create and duplicate: store the root ID on undo, and the serialized subtree for redo.
- [x] Delete: store the serialized subtree with its parent and sibling index.
- [x] Reparent and reorder: store the old and new parent and index.
- [x] Add or remove component: store the component's properties and bucket position.
- [x] Preserve entity IDs on restore, because selection and other commands refer to them.

### 3.5 Keep full snapshots where they belong

- [x] Keep snapshot entries for bakes, prefab instance updates, model instance refreshes and Play mode restore.
- [x] Let commands and snapshots coexist in one history, restoring correctly when undo crosses from one kind to the other.

### 3.6 Verification mode

The main risk is an edit that silently drops out of undo, because `FlushUntrackedSceneEdit` catches everything today and scoped capture will not.

- [x] Add a debug setting that takes a full snapshot beside each scoped command.
- [x] After each undo and redo, compare the scene to the expected snapshot and log any difference with the entry's label.
- [x] Keep the existing untracked flush as a safety net in this mode, and log when it finds a change no command recorded.
- [x] Run the mode through a scripted edit sequence in the test suite.

**Done when:** a transform or property edit shows no scene serialization in the profiler, in every test scene, and verification mode reports no differences across the scripted sequence.

## Phase 4: Remaining UI fixes

These are independent of the undo work and can land at any time. Estimate: about 1–2 days. None has been measured, so profile each one first and drop any that turns out cheap.

### 4.1 Clip the hierarchy

File: `SceneHierarchyPanel.cpp`, `RenderEntityNode` at line 196. Today every expanded row runs each frame, including a `std::string` name copy at line 368 and an icon lookup.

- [x] Flatten expanded entities into a row list, rebuilt when the hierarchy or expansion changes.
- [x] Draw the list through `ImGuiListClipper`, which the inspector and content browser already use.
- [x] Keep drag and drop, rename, keyboard navigation and scroll-to-selection working, since they assume the recursive tree.
- [x] Reuse the flat list for `VisibleEntityIds`.

### 4.2 Cheapen viewport icons

File: `ViewportPanel.cpp`, `DrawViewportEntityIcons` at line 323. Icons are on by default and walk every entity each frame.

- [x] Replace the `std::find` over the selection at line 348 with a set lookup, so it stops scaling as entities × selected.
- [x] Cache the list of entities that have icons, and rebuild it when the hierarchy or components change.

### 4.3 Keep hierarchy state across undo

File: `SceneHierarchyPanel.cpp:912`. Undo currently collapses the tree, because the scene swap bumps `m_sceneRevision` and clears expansion.

- [x] Separate "a different scene was opened" from "the same scene was restored", and clear expansion only for the first.
- [x] Keep this fix even after Phase 3, because full snapshots remain for scene-wide operations.

**Done when:** frame time with the hierarchy expanded stops growing with row count beyond what is visible, and undo leaves the tree as it was.

## Validation and open questions

Each phase is complete when its targets below hold on the same scenes as the baseline. The targets after Phase 1 are estimates until Phase 0 replaces them with real numbers.

| Measure | Today | After Phase 1 | After Phase 3 |
| --- | --- | --- | --- |
| Saves for an `ExecuteSceneEdit` | 3 | 1 | 0 for converted edits |
| Add component in `Verdant` | at least 10 s (estimated) | 0.5–1 s (estimated) | no serialization of foliage |
| Add component with 5,000 entities | 2.8 s | under 1 s | not dependent on entity count |
| Undo with 5,000 entities | ~2.7 s | ~1.8 s | not dependent on entity count |

After Phase 2, undo depth in `Standoff` should reach at least 8 steps, from about 4 today.

### Tests to run

- [x] `PlutoGESceneHistoryTests`, `PlutoGEMultiEntityEditTests` and the new scaling test after every phase.
- [x] `PlutoGEFoliageEditorTests` and the terrain layer tests after 1.3 and 1.4.
- [x] Load and save existing scenes after 1.4 and 1.5, checking the format version is unchanged and old files still open.
- [ ] Manual pass in the editor: gizmo drag, inspector edit, add component, duplicate, delete, undo and redo, Play and Stop, autosave recovery.
- [x] Check known baseline failures before calling anything a regression, since some RHI and project suites already fail at HEAD.

### Open questions

Answered ones have moved to the Status section.

- Should foliage instances eventually leave the scene text for a sidecar binary file? A raw copy of 760k instances took 6 ms against 3.1 s to encode, but this is a format change and is not part of this plan.
