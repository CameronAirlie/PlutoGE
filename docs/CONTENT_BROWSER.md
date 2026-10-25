# Content browser

## Using the browser

- **Navigate** with the folder tree, breadcrumbs, double-clicking folders, or
  the Back / Forward / Up buttons (Alt+Left, Alt+Right, Alt+Up; Backspace also
  goes up). Navigating opens the path to the current folder in the tree.
- **Views**: grid (thumbnails, scalable) or list (Name, Type, Size, Date
  Modified). Sort by any of those from the toolbar or the list headers; folders
  always come first and names sort naturally ("Level 2" before "Level 10").
- **Select** with click, Ctrl-click (toggle), Shift-click (range), Ctrl+A, or
  the arrow keys (Shift extends; Home/End jump). Escape clears the selection.
  The primary selected asset is shown in the details pane.
- **Create folders** with the toolbar button, Add > New Folder, the background
  or folder-tree context menus, or Ctrl+Shift+N. A new folder opens for renaming.
- **Rename** inline with F2 or the context menu. Files keep their extension so
  a rename cannot change an asset's type; Enter commits and Escape cancels.
- **Cut / Copy / Paste / Duplicate / Delete** (Ctrl+X / C / V / D, Del) act on
  the whole selection. Cut items are dimmed until pasted. Delete asks for
  confirmation and lists what will be removed.
- **Drag and drop** items onto folder cards, list rows, tree nodes or
  breadcrumbs to move them; hold Ctrl to copy. Dragging a selected item moves
  the whole selection. Assets still drag into the scene and hierarchy as before.
- **Import from the OS** by dropping files or folders from your file manager
  onto the editor window. They land in the folder under the cursor or the
  current folder, keep their names (numbered on collisions), and dropped
  .gltf/.glb/.fbx models are imported. Drop a model's folder to bring its
  textures and buffers; dropping a lone .gltf copies only that file. Drops onto
  detached (floating) editor windows are not received yet.
- **System integration**: Show in File Manager (Ctrl+Shift+R; selects the item
  in Nautilus, Dolphin, Explorer or Finder), Open with Default App, Copy Path
  (Ctrl+Alt+C) and Copy Asset Reference.

Shortcuts apply while the browser has keyboard focus; scene shortcuts (copy,
paste, duplicate, delete, undo) are suspended then so they never act on the
scene behind it. F5 still starts Play.

### How file edits treat assets

- Renames and moves use the asset move service, so identity sidecars
  (`.plutometa`) and mesh material overrides travel with their assets and
  asset IDs stay stable. Imported model objects and generated manifests
  cannot be moved independently (the service explains why).
- Copies drop identity sidecars, so every copy receives a fresh ID.
- Deletes remove an asset's sidecar and mesh material overrides with it, and
  wait if an import holds the project lock. They are permanent.
- Every edit is confined to the asset directory and refuses project
  infrastructure, the asset root and paths outside the project.

## Code layout

`ContentBrowserPanel` draws the views and routes input. Behaviour lives in
`editor/ui/{include/PlutoGE/ui,src}/contentbrowser/`:

| Module | Responsibility |
| --- | --- |
| `ContentCommands` | The command table: labels and shortcut chords for every action. |
| `ContentFileOperations` | Create, rename, move, copy, delete and import, with batch reports. |
| `ContentSelection` | Ordered multi-selection with anchor ranges, pruning and remapping. |
| `ContentNavigationHistory` | Back/forward history that follows renames and deletions. |
| `ContentSorting` | Sort keys and natural, folders-first ordering. |
| `ContentPaths` | Asset-relative folder helpers, name validation and unique names. |
| `ContentFormatting` | Size and timestamp display. |

`ui/SystemShell` opens and reveals paths with the operating system without
going through a shell, and `platform::Window::GetDroppedPaths()` exposes OS
file drops for the frame after they arrive.

Items are keyed by asset reference, or `folder://<relative path>` for folders,
so selection, clipboard and history survive renames: file operations report
`source -> destination` pairs and the panel remaps every key through them.
Anything triggered while items are drawn (menus, drops, renames) is queued and
runs after the item views finish, because file edits rebuild the item list.

### Extending

- **A new command**: add it to `ContentCommand` and the table in
  `ContentCommands.cpp`, then handle it in `CanExecute` and `ExecuteNow`. Menus
  use `CommandMenuItem`, so labels, shortcuts and enabled state come for free,
  and its chord works automatically while the browser is focused.
- **A new file operation**: add it to `ContentFileOperations`, returning a
  `ContentOperationReport`, and call `ApplyReport` so the registry refreshes and
  selections follow the change.
- **A new sort key or column**: extend `ContentSortKey`/`ContentSortFields`
  and add the column in `RenderItemList`.

## Verification

`PlutoGEContentBrowserTests` covers path helpers and name validation, natural
sorting and every sort key, selection ranges/toggling/remapping, history,
the command table, file URIs, and file operations against a temporary project
(unique folder names, sidecar-preserving renames and moves, self-move and
self-copy rejection, copies that drop identities but keep material overrides,
sidecar-aware deletes, protected paths, and imports with collisions).

Interactive checks: create and rename a folder; drag a multi-selection into a
folder with and without Ctrl; cut and paste across folders; delete a folder;
switch to list view and sort by each column; drop files from the OS onto a
folder card; reveal an asset in the file manager; confirm Delete in the
browser leaves the scene selection untouched.
