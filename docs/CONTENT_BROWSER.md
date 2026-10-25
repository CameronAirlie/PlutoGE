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
- **Cut / Copy / Paste / Duplicate** (Ctrl+X / C / V / D) act on the whole
  selection. Cut items are dimmed until pasted.
- **Delete** (Del) moves the selection to the system trash (Recycle Bin, Finder
  Trash, or the freedesktop.org trash on Linux) after confirming, so it can be
  restored. Shift+Del, or the dialog's second button, deletes permanently.
- **Drag and drop** items onto folder cards, list rows, tree nodes or
  breadcrumbs to move them; hold Ctrl to copy. Dragging a selected item moves
  the whole selection. Assets still drag into the scene and hierarchy as before.
- **Import from the OS** by dropping files or folders from your file manager
  onto any editor window, including detached (floating) ones. They land in the
  folder under the cursor or the current folder and keep their names (numbered
  on collisions). A dropped .gltf/.glb/.fbx brings exactly the files it
  references (buffers, textures) in a folder named after it, keeping their
  relative layout, and is then imported. **Add > Import > 3D Model...** copies
  the same way into `SourceModels/<name>`, never the rest of the folder the
  model was picked from. **Add > Import > Files...** picks any number of files
  with the system dialog and imports them into the current folder the same
  way as a drop.
- File dialogs (here and for scenes, projects and builds) use the native dialog
  on Windows and zenity or kdialog on Linux; when neither is installed the
  console says so instead of the menu silently doing nothing.
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
- Deletes take an asset's sidecar and mesh material overrides with it (into
  the trash too, so restoring all of them brings the asset back with its ID),
  and wait if an import holds the project lock. An item that cannot be
  trashed (for example on a drive without a trash) is kept, and the console
  suggests Delete Permanently.
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

`ui/FileDialogs` provides open, multi-open and save dialogs with typed
filters. `ui/SystemShell` opens, reveals and trashes paths with the operating system
without going through a shell. `platform::Window::GetDroppedPaths()` exposes OS
file drops for the frame after they arrive; the editor compositor forwards
drops from detached viewport windows to it. `assetimport::ModelSourcePackageCopy`
collects a model's dependencies through the importer and copies only those.

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
sidecar-aware deletes and trashing, protected paths, and imports with
collisions), the freedesktop trash (encoded info files, no replacement of
same-named items), file dialog arguments for zenity and kdialog, process output capture,
model package layout planning, and a real glTF drop that
must bring its buffer and leave unrelated neighbours behind.

Interactive checks: create and rename a folder; drag a multi-selection into a
folder with and without Ctrl; cut and paste across folders; delete a folder;
switch to list view and sort by each column; drop files from the OS onto a
folder card and onto a detached browser window; drop a .gltf with external
textures; trash an asset and restore it from the system trash; reveal an asset
in the file manager; confirm Delete in the browser leaves the scene selection
untouched.
