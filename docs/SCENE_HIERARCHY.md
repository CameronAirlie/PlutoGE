# Scene hierarchy

## Using the hierarchy

- Rows are full-width click targets at the theme's widget height (they follow
  the Compact/Default/Comfortable density setting). Each row shows an icon for
  its most specific component; inactive entities are dimmed.
- A collapsed row whose subtree contains the selection is tinted, shows an
  accent dot after its name and explains itself in a tooltip.
- Selecting an entity elsewhere (viewport pick, creation, undo) expands its
  ancestors and scrolls it into view. Clicks inside the hierarchy never reopen
  a parent you collapsed.
- Keyboard (while the tree has focus): Up/Down move the selection, Shift
  extends it, Left collapses or moves to the parent, Right expands or moves to
  the first child, Home/End jump to the ends, F2 renames, Enter frames the
  selection in the viewport.
- Alt-click an arrow to open or close a whole subtree. The buttons beside the
  search field, the empty-space context menu and each row's context menu offer
  Expand/Collapse All.
- Dragging a row onto the top or bottom quarter of another row places it before
  or after that row, shown by an insertion line; the middle of the row parents
  it. Dropping below an expanded parent inserts it as the first child. Dragging
  a selected row moves every selected root, keeping their order. Reparenting
  keeps local transforms, as before. Dropping on **Scene Root** moves to the end
  of the root list. Sibling order is saved with the scene.
- Hovering a row shows two toggles. The eye toggles the entity's active state
  (an undoable edit). The lock stops the entity and its descendants being picked
  in viewports while keeping them selectable in the hierarchy. Locks belong to
  the editor session: they are not saved and reset when another scene opens.
  Non-default toggles stay visible without hovering.
- Search and component filtering show matches plus the ancestors leading to
  them (muted). Results are cached and refresh when the criteria change, after
  hierarchy edits, and every 0.25 s to pick up edits made in other panels.

## Code layout

`SceneHierarchyPanel` draws rows and routes input; behaviour lives in
`editor/ui/{include/PlutoGE/ui,src}/hierarchy/`:

| Module | Responsibility |
| --- | --- |
| `HierarchyExpansion` | Expanded rows keyed by entity ID; the panel drives ImGui from it every frame. |
| `HierarchyNavigation` | Pure tree-view keyboard rules over the visible rows. |
| `HierarchyDropPlacement` | Drop zone classification, placement resolution and validated moves. |
| `HierarchyFilter` | Cached name/component matching. |
| `HierarchyEntityIcons` | Ordered icon rules; first match wins. |
| `HierarchyEntityPresets` | The Create menu's preset table. |

Rows queue structural edits (moves, creation, deletion, duplication) and the
panel runs them after the traversal, because those edits invalidate the
child vectors being iterated. Deferred edits re-resolve entities by ID.

Related editor state lives on `EditorShell`: `GetSelectionRevision()` (advances
on every selection change), `GetEntityLocks()` and `FrameSelectedEntity()`.
`Entity::GetSiblingIndex`/`SetSiblingIndex` expose authored sibling order.

To add an icon, append a rule to `kRules` in `HierarchyEntityIcons.cpp` (and the
glyph to `EditorIcons.h` and its `Ranges`). To add a preset, append a
definition to `kPresets` in `HierarchyEntityPresets.cpp`.

## Verification

`PlutoGESceneHierarchyTests` covers sibling ordering, drop zones and placement
(including expanded parents, cycles and block moves), keyboard navigation,
expansion, filter caching, selection revisions, locks and icon rules.

Interactive checks (OpenGL and Vulkan, each density): collapse a parent of the
selection and confirm the marker; pick a nested entity in the viewport and
confirm it is revealed; reorder by dragging above/below rows and undo it; drag a
multi-selection; lock an entity and confirm viewport picks pass through it;
toggle active state; navigate with the keyboard including F2 and Enter.
