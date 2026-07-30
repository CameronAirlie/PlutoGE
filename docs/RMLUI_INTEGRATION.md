# RmlUi runtime integration

PlutoGE supports an incremental migration from its native runtime UI to
document-driven RmlUi interfaces. Native and RmlUi canvases can coexist in the
same scene while HUDs and menus are moved over individually.

## Current integration slice

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

## Try the sample

1. Copy `samples/rmlui/hud.rml`, `hud.rcss`, and the referenced font into a
   project's asset directory. Keep their relative URLs consistent.
2. Add a `CanvasComponent` to an entity.
3. Set `Backend` to `RmlUi`.
4. Set `DocumentPath` to the project-relative `.rml` file.
5. Start the scene runtime.

The sample uses ordinary RML/RCSS and includes a CSS-based crosshair. Crosshair
spread can later be driven by changing the four arm offsets through the RmlUi
DOM or a data model.

## Migration plan

### Phase 1 — engine host (implemented)

Keep the existing component UI operational while establishing dependency,
rendering, input, text, document lifetime, and asset loading.

### Phase 2 — gameplay binding

Add a scene-scoped RmlUi data-model registry and C# wrappers for:

- loading/showing/hiding documents;
- finding elements by ID;
- setting text, attributes, classes, and style properties;
- binding observable C# fields to RmlUi model variables;
- subscribing to DOM events without polling.

This is the next required slice for migrating `FpsHudController` and
`ReactiveCrosshair`.

### Phase 3 — editor authoring

Add an RML preview/inspector panel, hot reload for RML/RCSS, asset creation
templates, DOM picking, and diagnostics linked to source locations.

### Phase 4 — native UI retirement

Migrate menus and HUD prefabs, retain a compatibility loader for old scenes,
then remove native layout/text/button rendering only after project content no
longer references it. World-space UI should remain a separate native render
surface or use RmlUi rendered into a texture.

## Intentional limitations of this slice

- RmlUi canvases are screen-space overlays; world-space documents need a
  render-to-texture path.
- Gameplay data binding and managed DOM APIs are not implemented yet.
- The official GL3 backend supports RmlUi's advanced rendering features, but
  visual regression coverage still needs to be added.
- Input consumption is not yet fed back into PlutoGE gameplay controls.
