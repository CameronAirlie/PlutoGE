# RmlUi runtime integration

PlutoGE supports an incremental migration from its native runtime UI to
document-driven RmlUi interfaces. Native and RmlUi canvases can coexist in the
same scene while HUDs and menus are moved over individually.

For a minimal setup walkthrough and copyable example, see the
[RmlUi project quick start](RMLUI_QUICKSTART.md).

## Current integration

- RmlUi 6.1 and FreeType 2.13.3 are pinned and built from source.
- The official OpenGL 3 renderer is executed after PlutoGE's native UI pass.
- `.rml`, `.rcss`, image, and font references are resolved relative to the
  loaded document.
- Mouse buttons, pointer movement, two-axis wheel input, keyboard transitions,
  Unicode character input, clipboard, cursor styles, and resize events are
  connected.
- A `CanvasComponent` selects either `Native` or `RmlUi`. RmlUi canvases set a
  project-relative `DocumentPath`; native layout and rendering skip that
  subtree.
- Documents are opened and closed automatically as active RmlUi canvases enter
  and leave the rendered scene.
- Documents honor their Canvas scale factor, reference resolution, and
  screen-match settings. `ScaleWithScreenSize` keeps the UI at a consistent
  proportion of the viewport across resolutions.
- RML and sibling RCSS changes hot reload while the document is active.
- Managed scripts can use `RmlDocument`, `RmlElement`, `RmlEvent`, and
  `RmlBindings` for DOM state, CSS classes/styles, native DOM events, and
  observable bindings.
- The Content Browser can create paired RML/RCSS assets, and the normal game
  viewport provides the live preview with source hot reload.
- UI-captured pointer/keyboard input is withheld from gameplay input polling.

## Try the sample

1. Copy `samples/rmlui/hud.rml` and `hud.rcss` into a project's `Assets/UI`
   directory. Copy `editor/resources/fonts/MartianMono-StdRg.ttf` to
   `Assets/UI/Fonts`, then change the RCSS font URL to
   `Fonts/MartianMono-StdRg.ttf`.
2. Add a `CanvasComponent` to an entity.
3. Set `Backend` to `RmlUi`.
4. Set `DocumentPath` to the project-relative `.rml` file.
5. Start the scene runtime.

The sample uses ordinary RML/RCSS and includes a CSS-based crosshair. Crosshair
spread can later be driven by changing the four arm offsets through the RmlUi
DOM or a data model.

## Pause menu sample

`samples/rmlui/pause-menu.rml` and `pause-menu.rcss` pair with
`Examples/RmlPauseMenuController.cs`. Copy the two assets into the project's
`Assets/UI` directory, add an RmlUi canvas referencing
`UI/pause-menu.rml`, and attach the controller to an always-active entity.
Configure its restart and main-menu scene references in the Inspector.

The controller pauses scene time, manages cursor capture, toggles with Escape,
and handles Resume, Restart, and Main Menu through native DOM click events.
Bundled gameplay controllers respect `GamePause.IsPaused`, preventing their
own Escape cursor handling from conflicting with the menu.

## Migration plan

### Phase 1 — engine host (implemented)

Keep the existing component UI operational while establishing dependency,
rendering, input, text, document lifetime, and asset loading.

### Phase 2 — gameplay binding (implemented)

Add a scene-scoped RmlUi data-model registry and C# wrappers for:

- loading/showing/hiding documents;
- finding elements by ID;
- setting text, attributes, classes, and style properties;
- binding observable C# fields to RmlUi model variables;
- subscribing to DOM events without polling.

This is the next required slice for migrating `FpsHudController` and
`ReactiveCrosshair`.

### Phase 3 — editor authoring (initial workflow implemented)

Asset templates, viewport preview, and RML/RCSS hot reload are implemented.
DOM picking and source-linked diagnostic presentation remain optional editor
enhancements rather than runtime migration blockers.

### Phase 4 — native UI compatibility

The FPS HUD has an RmlUi controller and document sample. Native components
remain as the compatibility and world-space path because serialized external
projects cannot be audited from the engine repository. Removing their
deserializer would corrupt those scenes. New screen-space UI should use RmlUi.

## Intentional limitations of this slice

- RmlUi canvases are screen-space overlays; world-space documents need a
  render-to-texture path.
- RmlUi data models are exposed through lightweight observable DOM bindings;
  direct reflection-based RmlUi C++ data-model registration is not required.
- The official GL3 backend supports RmlUi's advanced rendering features, but
  visual regression coverage still needs to be added.
- Input consumption is not yet fed back into PlutoGE gameplay controls.
