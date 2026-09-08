# Editor iteration tools

## Keep selected play-mode changes

1. Enter play mode and tune entity transforms or serialized component properties.
2. Choose **Runtime > Stop and Keep Changes...**.
3. Review the original and captured values. Nothing is selected initially.
4. Check the individual values to retain and choose **Apply Selected**, or choose
   **Discard** to keep the original scene.
5. Undo/redo the retained values as one **Keep Play Mode Changes** history entry.

Ordinary **Stop** / **Shift+F5** still restores the pre-play scene. The editor
preserves the authoring undo/redo history and discards temporary play history on
stop. Discarding a review preserves the pre-play scene's dirty state.

The review includes name, active state, local position/rotation/scale, component
enabled state, and properties exposed by component serialization. Script values
follow that serialization contract: inspector-authored serialized fields are
included; arbitrary managed runtime state is not copied.

This workflow retains values on existing objects. It does not retain entity
creation/deletion, parenting, component layout changes, script class changes,
tags, scene-wide settings, or shared asset edits. Replacing the play scene (through
a scene load or a snapshot-based runtime undo) disables retention for that session.
Local transforms on reparented entities and references to runtime-only entities
are unavailable. Existing prefab override paths support only the first component
of a repeated type on a prefab instance; additional instances are excluded.

Application validates against the restored scene and stages changes on a detached
scene before publishing. A validation failure leaves the active scene untouched
and reports the problem in the review. The tool does not save the scene to disk;
use the normal scene save workflow afterward.

## Viewport bookmarks

Open **View > Viewport Bookmarks** with a project loaded. Enter a name and choose
**Save Current View**. Select a saved view to **Recall**, **Replace View**,
**Rename** using the name field, or **Delete** it.

Bookmarks include position, yaw/pitch, lens clipping planes and field of view,
movement speed, and perspective/orthographic projection with orthographic size.
Recall affects the editor viewport camera. Scene cameras and post-process settings
are unaffected.

Changes save immediately to
`.plutoge-editor/<project-manifest-filename>.bookmarks` beside the project's asset
directory. This file is editor data and is outside the runtime asset registry.
Each project supports 128 unique named views. Existing scene and project formats
are unchanged.

The file has a versioned header, bounded input size, finite camera-value checks,
and validation of names and lens ranges. Saves write a temporary file before
replacing the existing file. A failed save retains the previous collection; a
malformed file disables bookmark editing until it is repaired and **Retry loading**
succeeds. Back up the sidecar if you want to recover intentionally deleted views.

## Drop selection onto ground

Select an entity, then choose **Edit > Drop Selection onto Ground...**. Configure
the maximum search distance, surface offset, normal alignment, and optional yaw
and scale variation, then choose **Drop Selection**. The action is available in
edit mode while no scene bake is running. Undo/redo covers the entire transform.

The downward query starts just above the selected pivot and excludes the selected
entity and its descendants. Target terrain or geometry must have enabled,
non-trigger colliders. No hit leaves the selection unchanged.

By default the selected hierarchy's static mesh bounds and primitive colliders
determine contact with the plane at the hit. **Place pivot on surface** bypasses
those bounds, and is also the fallback when the hierarchy has no supported bounds.
This is a placement tool for static props: it does not evaluate animated poses,
simulate settling, or fit every corner to uneven terrain.

Normal alignment points the local up axis toward the surface normal. Placement
preserves the pivot's world X/Z coordinates and converts the final position back
into parent space. Singular parent transforms are rejected.

Scale factors multiply the existing local scale; a range of 1–1 leaves scale
unchanged. Random yaw is bounded by the configured angle on either side of the
current orientation. The seed gives reproducible planning from the same original
transform and advances after each successful drop. Repeated drops with scale
variation therefore compound scale; undo before retrying if that is unwanted.

## Find asset references

Right-click an asset in the Content Browser and choose **Find References...**.
The Asset References window groups incoming references by owning file, showing
the first text line (or **Binary**) and the number of occurrences. **Show in
Browser** reveals the owner; **Open** also opens its existing asset editor when
one is available. Imported model manifests navigate to their source model.

Results describe saved files under the current project's Assets directory. Save
scene and asset edits before searching. A cancellable background worker checks
for additions, edits, moves, and deletions approximately once per second after
each scan. Unchanged files reuse cached results. **Rescan all** bypasses file size
and modification-time checks, including for external tools that preserve both.
Closing the search or changing projects cancels its worker. Searching does not
write metadata, reimport assets, or modify scenes.

Extraction covers native scene, prefab, material, mesh, animation, graph,
particle, post-process, scriptable-object, input, and model-manifest formats.
It preserves spaces and punctuation in references, resolves asset-relative
material textures, and streams binary reference strings even beyond 16 MiB.
Quoted literal references in C#, RML, RCSS, and glTF are also searchable; RML
`src`/`href` and glTF `uri` paths resolve relative to their owner.

This is a saved-reference search, not a complete dependency validator. It does
not evaluate computed script paths, CSS URL syntax, external files, or references
from other projects. Binary matches recognize length-prefixed reference strings;
text matches recognize serialized fields and literals. Malformed structures are
not fully validated. Read failures, interrupted file changes, truncated reference
strings, and text records over 1 MiB (64 MiB for scenes/prefabs) appear under **Scan issues**, with an explicit
incomplete-results message. A result count of zero is not proof an asset is unused.

The cooker shares this extraction logic. Pruned cooking refuses to proceed when
a reachable asset has scan errors, to avoid silently dropping dependencies;
include-all cooking remains available.

## Autosave and recovery

Open **Edit > Autosave and Recovery...** to configure autosave or recover a backup.
Autosave defaults to every 120 seconds with ten backups retained per project.
The interval supports 10–3600 seconds and retention supports 1–50 backups. Changes
apply for the current session; **Save Settings** persists them. **Back Up Now**
also backs up a clean scene. Unchanged scene/source pairs skip duplicate backups.

Backups live in `.plutoge-editor/<manifest-filename>.recovery` beside the project
manifest. They include untitled scenes and remain available after normal exits.
The recovery window opens when an opened project has backups or storage issues;
it does not infer whether the previous session crashed. Retention is shared across
the project's scenes, so save important recovered work into a regular scene file.

Select a dated backup and choose **Recover as Unsaved Scene**. The editor checks
its length, checksum, scene header, and deserialization result before replacing
the current scene, using the normal unsaved-changes prompt. Recovery starts a new
history and keeps the backup. Saving a recovered scene, including through Save
Project, requires choosing a path. Original scene files are never overwritten by
autosave or the recovery action.

Autosave runs only with an open project, a dirty authoring scene, and no active
play session, bake, or editing gesture. It waits for the gesture to finish. Scene
capture and file publication run on the editor thread, so very large scenes can
briefly pause the editor. Individual scene payloads are limited to 256 MiB.
Backups use a temporary file followed by atomic publication. Interrupted temporary
files are ignored; missing, truncated, or corrupt backups report errors. Rotation
runs only after a new backup is published. Malformed-header files are retained for
manual investigation. Malformed settings disable autosave until settings are saved.
Backups cover serialized scenes, not unsaved external material/texture/script files.

## Undo and redo coverage

**Ctrl+Z** undoes; **Ctrl+Y** or **Ctrl+Shift+Z** redoes. Scene history includes
existing explicit actions and now collects serialized changes that mark the scene
dirty but previously bypassed history: inspector names, transforms, component
properties/enabled states, scene environment settings, and other serialized scene
edits. A continuous drag or text edit forms one entry when interaction ends.
Explicit gizmo, canvas, ground-placement, structure, and foliage commands keep
their existing gesture labels and command boundaries.

Undo/redo completes a pending gesture first. A new edit clears the redo branch;
failed snapshot/command application retains the entry for retry. Snapshot restores
preserve selection by entity ID when that entity exists and avoid refreshing
prefabs over the restored state. History is retained across scene/project saves;
returning to the saved serialized state clears the scene's dirty indicator. Opening
another scene resets history, and play-mode history stays separate from authoring.

Coverage follows scene serialization. Asset-file writes, texture-paint pixels,
project settings, editor camera changes, and generated bake files are not restored
by scene snapshots. Existing dedicated foliage commands handle instance data.
History retains up to 80 entries with a 256 MiB snapshot/command budget, retaining
at least the newest entry; baseline/savepoint snapshots add memory outside that
budget. Undo/redo is unavailable during play or baking.

## Project validation

Open **Edit > Project Validation...**, then choose **Validate Project**. The panel
checks saved assets under the project's Assets directory and the current scene's
serialized state, including unsaved edits. The current scene replaces its saved
version for that run. Results show severity, a stable diagnostic code, owning
asset, entity ID, and line where available. **Show warnings** filters warnings.

**Show Asset** reveals the owner in the Content Browser. **Select Entity** selects
it in the current scene or opens the owning scene/prefab through the normal
unsaved-changes prompt. Results are snapshots: rerun after edits, scene switches,
imports, or script builds. Navigation from a changed untitled scene requires a
fresh validation so IDs cannot point into an unrelated untitled scene.

The initial checks cover:

- Missing startup scene references, configured script assemblies, and explicit
  project/engine asset references. Built-in assets are recognized without files.
  Non-scene dependencies use the M04 extractor, including relative material paths.
- Missing script classes when the current assembly's class catalogue is available.
  Empty script assignments and unavailable catalogues are warnings. Build/reload
  scripts before relying on class diagnostics; script source code is not compiled
  by an on-demand validation run.
- Scenes without an enabled camera on an active parent hierarchy. This is a
  warning because gameplay can create cameras dynamically; prefabs are exempt.
- Invalid collider shape, center, dimensions, capsule proportions, zero/non-finite
  hierarchy scale, or missing enabled terrain/mesh source components.
- Invalid scene headers, malformed records, missing component owners, duplicate
  entity IDs, missing parents, cycles, and incomplete scans.

**Build Project** and **Build and Run Project** validate saved project data after
saving and building scripts, before rebuilding/copying the runtime. Errors stop
export and open the panel. Warnings permit export. All saved assets are checked,
including unused assets. Fix errors and build again; there is no stale-result
bypass. Other callers of the low-level export API must invoke validation themselves.

Validation is read-only and does not instantiate scenes, start scripts, write
metadata, or repair files. It runs synchronously on demand, so large projects can
pause the editor during the scan. Scene files are limited to 256 MiB and individual
scene/prefab records to 64 MiB (to accommodate inline terrain samples); exceeding either produces an export-blocking incomplete-scan
diagnostic. The checks do not prove runtime correctness, validate every component
property, inspect mesh collision geometry, or resolve computed script references.
Legacy external/relative scene paths outside explicit asset-reference syntax are
not checked. A clean report means these available checks found no issues.

## Gameplay debug drawing

Use `PlutoGE.ScriptCore.DebugDraw` during Play to submit world-space lines, wire
spheres, and labels. They appear in both editor Scene and Game views. Open
**Edit > Gameplay Debug Drawing...** to toggle all overlays, filter named
categories, inspect native rejection counts, or clear the drawings/categories.
The Game view needs an active scene camera.

```csharp
using System.Numerics;
using PlutoGE.ScriptCore;

public sealed class DebugDrawingExample : ScriptBehaviour
{
    public override void OnCreate()
    {
        DebugDraw.Label(Vector3.UnitY, "Spawn point", Vector4.One, 2, "Spawns");
    }

    public override void OnUpdate(float deltaTime)
    {
        if (deltaTime <= 0) return;
        DebugDraw.Line(Vector3.Zero, Vector3.UnitY * 2,
                       new Vector4(0, 1, 0, 1), category: "Physics");
        DebugDraw.Sphere(Vector3.Zero, 0.5f,
                         new Vector4(1, 0.5f, 0, 1), category: "Physics");
    }
}
```

Colors are RGBA and clamp to [0,1]. Duration zero lasts through the current editor
frame. Positive durations use simulation seconds and age once after the editor
frame, independent of how many viewports are visible. Time scale zero freezes
both timed and one-frame drawings. Avoid submitting repeatedly while paused.
Hidden drawings still expire when simulation advances. A command submitted by a
background thread after viewport rendering may miss that frame; submit frame-only
drawings from gameplay callbacks. Closed viewports do not defer expiry.

The store accepts at most 4096 commands and 64 named categories. Categories stay
registered until cleared, even after their commands expire. Category names are
limited to 64 UTF-8 bytes and labels to 256; embedded nulls are rejected. Durations
must be finite and between zero and 3600 seconds, sphere radii in (0, 1000000],
and position coordinates within ±1000000000. Submission returns `false` when
invalid, full, unregistered, or outside Play. Native rejections increment the
counter; managed text-length checks reject before crossing the bridge.

Play start/stop, scene replacement, and script shutdown/reload clear commands and
category state. Global overlay visibility persists within the editor process.
`DebugDraw.Clear()` also clears commands/categories. The storage copies command
data under a mutex; no scene pointers or managed string pointers are retained.

Rendering reuses clipped world-space drawing helpers and the existing ImGui
compositor for OpenGL and Vulkan. Spheres are three wire circles; labels are
screen-facing text. Overlays draw through geometry and do not perform depth tests.
This milestone supplies editor gameplay visualization, not a standalone-player
debug overlay or persistent scene data. Standalone native hosts can consume the
same command store through a future renderer. Rebuild the scripting SDK and game
scripts together to pick up the new managed/native registration entry point.
