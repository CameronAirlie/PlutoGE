# Multi-entity editing

- Click an entity to select it. Ctrl-click toggles membership in both the hierarchy
  and viewport. Clicking empty space clears selection.
- Shift-click in the hierarchy selects the inclusive range of visible rows from
  the last selection anchor. Collapsed children are excluded. Ctrl+Shift-click
  adds a range to the current selection. Shift-click in the viewport adds an entity.
- The last selected entity remains the primary selection. Selecting the editor
  camera or opening another scene clears the entity selection. Group selections
  show blue origin markers in the viewport, with the primary marker in yellow.
- The group gizmo moves, rotates, and scales the selection around the average
  world position of its selected roots, using world axes and the existing snap
  settings. A selected child inherits its selected parent's movement once.
  Transforms requiring shear or singular parent inversion are rejected for the
  whole selection, with an explanation in the status bar.
- The inspector edits names, active state, and local position/rotation/scale.
  Mixed fields show the first selected entity's value and are labelled mixed.
  Editing an axis assigns that value to every selected entity while retaining
  each entity's other axes. Local inspector edits apply explicitly to every
  selected entity, including children.
- Common components are matched by component type and occurrence within that
  type. Only properties with matching names, types, and enum options are shown.
  Component enabled state and serialized properties can be edited together;
  vector axis edits preserve the other axes. Component-specific tools and shared
  asset editors remain available through single selection.
- Duplicate and Delete operate on selected roots as one undoable action. Ctrl+G
  groups selected entities. Copy/paste and hierarchy reparenting retain their
  existing single-entity behavior.
- Inspector gestures and group gizmo drags use the scene history's gesture
  coalescing, so one drag is one undo step. Undo/redo preserves selected IDs that
  exist in the restored scene. Selection changes alone do not create history.

## Verification

`PlutoGEMultiEntityEditTests` covers Ctrl toggling, visible ranges, stale IDs,
root filtering, parented movement, signed scales, rotation, atomic rejection of
invalid transforms, common component intersection, preservation of unrelated
values, per-axis property editing, and snapshot undo/redo of multiple entities.

Interactive checks (OpenGL and Vulkan): select an upward/downward range with
collapsed children; move a selected parent and child together; rotate and scale
two roots; edit a mixed transform axis and common light intensity; undo/redo each
gesture; duplicate and delete a selection; then switch scenes and select the
editor camera. Also check ordinary single-entity gizmos and component tools.
