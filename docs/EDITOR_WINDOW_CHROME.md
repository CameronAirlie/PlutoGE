# Editor window chrome

The editor draws its own title bars, themed with the active editor theme, on the
main window and on every detached window. The Windows title bar is removed but
the native frame stays, so the windows still behave like any other Windows app.

## Using it

**Main window.** The title bar holds, from left to right: the PlutoGE mark, the
main menu, the project name (with a dot when there are unsaved changes), the
Play/Pause/Step/Stop controls, the status line and the window controls. The bar
takes the play-mode accent while playing or paused.

**Detached windows.** Drag a panel out of the main window and release it: it
gets its own window with a title bar and a dock space. Dock more panels into
it, split it, or drag tabs back out. The title shows the focused panel. Next
to the window controls:

- **Dock into main window** moves every panel in the window back into the main
  window.
- **Keep on top** keeps the window above other windows. The setting is saved
  with the layout.

Closing a detached window closes its panels. A detached window disappears once
its last panel is closed or dragged out. Detached windows, with their position,
size and docking, are saved in the project layout. **Reset layout** docks them
all back into the main window.

**What still works as on Windows:**

| Action | Result |
| --- | --- |
| Drag the title bar (anywhere that is not a control) | Moves the window; drag to a screen edge to snap |
| Double-click the title bar | Maximises or restores |
| Hover the maximise button (Windows 11) | Shows snap layouts |
| Right-click the title bar, click the PlutoGE mark, or press Alt+Space | Opens the system menu (Restore, Move, Size, Minimise, Maximise, Close) |
| Drag a window edge or corner, including the top edge | Resizes |
| Win+Arrow keys, Alt+F4, taskbar and Alt+Tab | Unchanged |

The window border colour and the light/dark style of OS-drawn parts (for
example the snap layouts flyout) follow the editor theme on Windows 11.

On platforms other than Windows the system title bar is kept, the main menu
sits below it, and panels dragged out of the main window stay ordinary floating
windows.

## How it is built

| Piece | Responsibility |
| --- | --- |
| `platform::CustomWindowFrame` (`engine/platform`) | Win32 subclass that turns the caption into client area (`WM_NCCALCSIZE`) and answers `WM_NCHITTEST` from the caption layout the editor reports each frame. Keeps the side and bottom frame for resizing and shadow, reports the maximise button as `HTMAXBUTTON` for snap layouts, and shows the system menu. Also places windows by their client rectangle, since GLFW assumes a system caption. |
| `WindowConfig::customTitleBar` | Asks `platform::Window` to attach a frame to the main window. `GetCustomFrame()` returns it. |
| `ui::EditorWindowChrome` | Wraps ImGui's platform window hooks so each decorated detached viewport gets a frame, and corrects GLFW's placement of those windows. Syncs the theme to the OS-drawn frame. |
| `ui::caption` (`WindowCaption.h`) | Shared caption metrics (Windows 11 proportions scaled by the editor font), colours, glyphs, buttons, the window controls and `LayoutBuilder`, which reports a caption to its frame. |
| `ui::EditorTitleBar` | The main window bar. Replaces `ImGui::BeginMainMenuBar`; owned by `PanelManager`. |
| `ui::DetachedWindowHosts` | Adopts panels left floating outside the main window into host windows with a dock space, draws their captions, and saves them through an ImGui settings handler. |

Each frame, a title bar reports where it drew itself: the whole caption, the
parts that must keep ordinary input (menus, buttons, an elided status), and the
maximise button. Everything else in the caption drags the window. Anything
added to the main title bar must be marked with `EditorTitleBar::AddInteractiveItem()`
or it will drag the window instead of receiving clicks:

```cpp
auto &titleBar = panelManager.GetTitleBar();
if (titleBar.BeginMenuBar())
{
    // ImGui::BeginMenu(...) ...
    titleBar.EndMenus();
    DrawMyToolbar();                // one item or group
    titleBar.AddInteractiveItem();
    titleBar.Status(statusMessage);
    titleBar.EndMenuBar();
}
```

`PanelManager::EndPanelUpdate` finishes the bar every frame, so the window
controls are present in frames without menus as well (project launcher, build
progress).
