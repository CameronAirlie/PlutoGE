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
