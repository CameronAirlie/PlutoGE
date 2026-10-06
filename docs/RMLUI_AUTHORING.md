# RML UI authoring for PlutoGE developers and agents

This is the working guide for creating runtime UI in PlutoGE, including the CoD project. Read it before generating or modifying RML, RCSS, or managed UI controllers. For the complete pinned property and extension inventory, see [RML/RCSS reference](RMLUI_REFERENCE.md). For host internals, see [integration](RMLUI_INTEGRATION.md). The engine pins **RmlUi 6.1**; current upstream documentation can describe newer features. The pinned source and the engine renderer are authoritative.

## Create a document

Create `Assets/UI/screen.rml`, `screen.rcss`, and licensed fonts/images. The Content Browser can generate paired RML/RCSS assets. Select the RML asset on an enabled **RmlUi Canvas**, or an **RmlWidget**. Widgets inherit the nearest enabled Canvas scale policy. A document-owning Canvas owns its UI subtree; do not attach a second widget for the same document unnecessarily. Add stable unique IDs to elements accessed by scripts.

```xml
<rml>
<head>
    <title>Game interface</title>
    <link type="text/rcss" href="screen.rcss"/>
</head>
<body>
    <div id="screen">
        <div id="damage" class="hidden"></div>
        <div id="vitals" class="card"><label>HEALTH</label><span id="health-value">100</span></div>
        <div id="settings" class="card">
            <label for="volume">Master volume</label>
            <input id="volume" type="range" min="0" max="1" step="0.01" value="1"/>
        </div>
    </div>
</body>
</rml>
```

```css
@font-face { font-family: GameUI; src: url("Fonts/GameUI.ttf"); }
body { width: 100%; height: 100%; margin: 0; font-family: GameUI; font-size: 16dp; color: white; }
div { display: block; }
#screen { position: absolute; left: 0; top: 0; width: 100%; height: 100%; }
.card { position: absolute; box-sizing: border-box; padding: 16dp; background-color: #101722ee; }
#vitals { left: 24dp; bottom: 24dp; width: 240dp; }
#settings { right: 24dp; top: 24dp; width: 280dp; }
#damage { position: absolute; left: 0; right: 0; top: 0; bottom: 0; box-sizing: border-box; border: 12dp red; pointer-events: none; }
.hidden { display: none; }
input.range { display: block; width: 100%; height: 24dp; }
input.range slidertrack { height: 6dp; margin-top: 9dp; background-color: #38404a; }
input.range sliderprogress { background-color: #ffb31a; }
input.range sliderbar { width: 12dp; height: 24dp; margin-top: -9dp; background-color: #ffb31a; }
input.range sliderarrowdec, input.range sliderarrowinc { display: none; }
```

The examples assume Canvas **Constant Pixels**, scale factor 1, and screen overlay render mode. An RmlWidget without a Canvas also uses scale 1. This keeps physical viewport units meaningful. A separate Canvas resolution transform is still available for legacy/reference-resolution authoring.

## Interface scale and coordinate spaces

Set the preference once, then reapply on scene/play-session initialization:

```csharp
if (UISettings.IsSupported)
    UISettings.TrySetInterfaceScale(playerPreferences.InterfaceScale);
```

`UISettings.InterfaceScale` reads the live preference. `TrySetInterfaceScale` accepts finite values from **0.5 to 3**, returns false when unavailable, and throws for invalid arguments. Native `RmlUiRuntime::SetInterfaceScale` returns false for invalid values. The engine does not persist preferences; the game owns storage. The value works before renderer initialization, survives renderer backend recreation, and resets to 1 when runtime state is reset.

The screen context uses this value as RmlUi's density-independent pixel ratio. Layout, text sizes, control geometry, wrapping, hit testing, and flow spacing update through normal RmlUi layout. There is no per-widget scale list.

| Authoring intent | Unit or layout rule |
|---|---|
| Scalable text, fixed panel dimensions, padding, margins, icons, borders, slider tracks | `dp` |
| Anchor to a corner | Absolute positioning with `left`/`right` and `top`/`bottom` |
| Fill the viewport | Full-size untransformed root; `left: 0; right: 0; top: 0; bottom: 0` |
| Center in available space | Flex centering, or 50% anchor with matching dp half-size margins |
| Proportion of parent | `%` |
| Projected crosshair spread, angular indicator position | `vh` / `vw` or physical coordinates |
| Physical one-pixel geometry and fixed viewport measurements | `px` |
| Relative text measurements | `em` / `rem`; these follow their font size |
| Viewport responsive breakpoints | Usually `px`; test at largest interface scale |

`dp` controls size; anchors control position. Scaling a root using `transform: scale(...)` magnifies existing layout and does not provide normal reflow. Do not transform a full-screen HUD to implement player interface size. Damage/death overlays remain in a full-size viewport layer. Crosshair arm geometry may stay in px and spread in vh so aiming feedback continues matching the camera projection.

Screen and world documents now have separate contexts. WorldSpaceOverlay and WorldSpace use ratio **1** regardless of player interface size. Screen documents render above world overlays; input routes to the upper interacting layer and stays with the selected context during a pointer drag. Loading documents and isolated editor previews retain their own ratio 1 contexts. This preference is a player setting, not automatic operating-system DPI detection.

Canvas scale mode remains separate: ConstantPixels uses its factor, ScaleWithScreenSize combines factor and reference-resolution match, and ConstantPhysicalSize currently uses the stable 96-DPI baseline. A Canvas transform scales all its coordinate units, including px/vh. Combining that transform with dp adds two scales. Use ConstantPixels/factor 1 for a screen HUD whose projected geometry must be physically accurate.

## RML structure and markup features

RML is XML-style markup, not a browser document. Use one `rml`, a `head`, and a `body`. The head supports a title, linked/inline styles, templates, and script declarations in RmlUi; PlutoGE gameplay uses C# and does not install a JavaScript interpreter. Inline `onclick="some JavaScript"` is not a managed callback. Connect handlers with `OnClick` instead.

Close elements consistently, quote attributes, escape `&`, `<`, and `>` in text, and use character references where needed. Comments are supported. Generic element names are allowed but have no automatic semantics: define display and styles for your own tags. Common div/span/button/heading names are useful; RmlUi has no browser user-agent stylesheet that guarantees familiar default styling. RmlUi's optional HTML stylesheet is an authoring convenience, not part of PlutoGE's theme.

Global attributes include `id`, `class`, inline `style`, event-handler attributes, and control-specific attributes. IDs must be unique within a document. Classes are the preferred state mechanism (`selected`, `hidden`, `low-ammo`). Stable IDs survive native document reload because managed wrappers resolve IDs for each operation. Arbitrary attributes are accessible from managed code. Engine-reserved `data-*` extensions are listed below; do not reuse them for unrelated state.

Images use `img src` with explicit display dimensions; sprite selection uses the sprite-sheet mechanism. Relative RCSS/font references are resolved from the loaded source file; UI document paths accepted by PlutoGE include `UI/screen.rml` (Assets-relative) and `project://UI/screen.rml`. Assets must be registered/included in the project before export. Verify texture URLs against the actual asset resolver and renderer; do not assume browser/network image loading.

Templates let documents share structure through linked `.rml` template resources, `<template>`, and named insertion content. Built-in tabsets use `<tabset>`, `<tab>`, and `<panel>`; style selected panels and navigation. A manually managed tab bar, as in CoD, is also valid and uses classes to control section display. `<handle>` can move/resize its configured target and is styled like other controls. These native features do not require JavaScript. See the versioned feature inventory and upstream [markup manual](https://mikke89.github.io/RmlUiDoc/pages/rml.html) for detailed attributes.

## Forms, controls, and accessibility

| Element / input type | Use and authoring requirements |
|---|---|
| `button`, `input type="button"` | Action; provide label and visible hover/focus/active states |
| `input type="submit"`, `form` | Native form submission; bind submit in a controller |
| `input type="text"` | Text input; initialize value, size it, use maxlength where appropriate |
| `input type="password"` | Masked text field; do not expose its value in diagnostic text |
| `input type="checkbox"` | Boolean with checked state and visible label |
| `input type="radio"` | Named mutually exclusive selection group |
| `input type="range"` | Slider with finite min/max/step/value; visible value readout |
| `select`, `option` | Drop-down choice; style generated selection/arrow/list elements |
| `textarea` | Multiline entry; style selection, caret, scrollbars, dimensions |
| `label for="id"` | Associate explanatory text with its control |
| `progress` / `progressbar` | Value/max display; style its generated fill |

Keyboard input, Unicode text, cursor shapes, clipboard, mouse buttons, and wheel input are connected by the engine. Use focusable controls and a logical order. RML `tabindex` is the input attribute; RCSS `tab-index` controls focus eligibility. Engine Tab traversal selects newly focused text inputs. Always provide focus styling and avoid using color alone to communicate state. Game controllers should respect UI input capture and cursor locking.

Control appearance is authored explicitly. Sliders generate `slidertrack`, `sliderbar`, `sliderprogress`, `sliderarrowdec`, and `sliderarrowinc`. Style them, including the draggable bar and focus state; hiding arrows removes the old step-button interaction. Scroll containers generate horizontal/vertical scrollbars, tracks, bars, arrows, and a scrollbar corner. Give generated scrollbars explicit thickness (for example `scrollbarvertical { width: 8dp; }` and `scrollbarhorizontal { height: 8dp; }`) and style their tracks/bars. An unstyled scrollbar can consume the available content width when overflow first appears, collapsing a previously valid grid. Drop-downs generate `selectvalue`, `selectarrow`, and `selectbox`. Text controls generate caret/selection elements. The [core control style guide](https://mikke89.github.io/RmlUiDoc/pages/style_guide.html) covers their full structure.

For sliders, subscribe to `change`, read the current value attribute using invariant culture, validate/clamp in the settings model, then apply. Programmatic value updates can emit change events, so compare with the committed preference to avoid loops. The managed bridge queues event counts rather than preserving each event payload; read current control state and use it idempotently.

## Layout and RCSS

RCSS supports selectors/cascade/inheritance, normal and absolute positioning, the box model, flex layout, tables, overflow, clipping, visibility, z-index, backgrounds, borders, rounded corners, font/text layout, transforms, transitions, animation keyframes, media queries, decorators, sprite sheets, font effects, and renderer-dependent masks/filters. The [pinned reference](RMLUI_REFERENCE.md) lists every registered core property and shorthand.

Use block/flex layout for settings grids. RmlUi 6.1 has flex layout, including wrapping and gap; browser CSS Grid is not supported. Give columns explicit widths or flex-basis and appropriate flex-shrink to keep controls from collapsing. Put description text in its own block, rather than letting a button flow into the same text line. Use box-sizing border-box for cards; keep control columns consistent.

```css
.setting-row { display: flex; align-items: center; width: 100%; padding: 16dp; box-sizing: border-box; }
.setting-copy { width: 58%; flex-shrink: 0; padding-right: 20dp; box-sizing: border-box; }
.setting-control { width: 42%; flex-shrink: 0; }
@media (max-width: 700px) {
    .setting-row { flex-direction: column; align-items: flex-start; }
    .setting-copy, .setting-control { width: 100%; }
}
```

Responsive layouts should wrap, scroll, or collapse columns when space is limited. A dp panel becomes physically larger as the player scale rises; max-width/max-height percentages and scroll containers bound it. Scale-aware media queries may use the resolution feature; the screen context's dp ratio supplies that resolution. Test wide, narrow, short, and large-scale viewports.

Selectors include element, class, ID, attribute, combinators, structural pseudo-classes, and interactive pseudo-classes. Supported states include hover/focus/active and checked/disabled for relevant form controls. Use display:none to remove hidden tabs from layout; visibility:hidden retains their space. pointer-events:none makes decorative overlays transparent to input. focus:none is useful for noninteractive HUD documents.

Animation uses RmlUi's transition/animation syntax and @keyframes. Transforms support translate, scale, rotate, perspective and transform origin; apply them for visual animation or local zoom rather than global preference sizing. RCSS units include physical px, scalable dp, font-relative units, percentages, and viewport units. Newer upstream CSS-like conveniences are not automatically available: pinned 6.1 does not implement browser CSS custom properties/var(). CoD uses a token compiler (`theme.tokens.json` and `.rcss.in`) instead. Do not emit unsupported browser properties silently.

Fonts must be loaded; there are no guaranteed system fonts. PlutoGE scans linked RCSS and inline declarations for its `@font-face` convenience and loads relative font files before constructing documents. Provide the family/style/weight used by the theme and licensed fallback faces when needed. Text layout supports alignment, whitespace handling, transforms, letter spacing, line height, decoration, overflow and shaping according to the configured font engine. RmlUi font effects include shadow, outline, glow, and blur. Managed `UITween`, `UIEase`, and `UIEasing` provide lightweight game-driven numeric easing; update DOM styles with the resulting value rather than creating a new controller per frame. Color/bitmap fonts and large glyphs may use fallback rasterization behavior.

Decorators provide images, tiled strips/boxes, ninepatch panels, gradients, text, and shader-driven visuals. Sprite sheets name image regions for icons and decorators. Filters include blur, drop-shadow and color adjustments, and masking uses decorator images. **Parsing support is not rendering support**: PlutoGE's official legacy GL3 path implements advanced rendering features; the shared RHI renderer currently implements base geometry/images/fonts and clipping/masks, without general filter-layer or custom shader-decorator callbacks. Avoid relying on blur/backdrop filters or shader decorators in Vulkan/RHI UI until implemented. Use solid/translucent panels and image assets for portable designs. SVG/Lottie/Lua are optional RmlUi integrations and are not exposed as guaranteed game-authoring capabilities here.

## Managed controllers and lifetime

```csharp
using System.Globalization;
using PlutoGE.ScriptCore;

// Match the path selected by the scene's Canvas or Widget.
var document = new RmlDocument("UI/screen.rml");
document.OnClick("continue", () => SceneManager.LoadScene("Main"));
var volume = document.Element("volume");
volume.On("change", () => {
    if (float.TryParse(volume["value"], NumberStyles.Float, CultureInfo.InvariantCulture, out var value))
        ApplyMasterVolume(Math.Clamp(value, 0, 1));
});
document.Element("health-value").Markup = "85";
document.Element("damage").SetClass("hidden", false);
```

`RmlDocument` references a document owned by an active scene Canvas/Widget; its constructor does not load arbitrary files. DOM mutations before native creation can return no effect. Apply initial presentation again once the scene/document is live, and refresh values when opening a menu. Bind each controller once. Dispose documents you create to disable their event subscriptions; a widget-owned document follows the widget's lifetime. A hidden document is not automatically disabled for managed event dispatch, so clear stale state or explicitly disable dispatch where needed.

`RmlElement.Markup` sets inner RML, not escaped plain text. Escape untrusted/user strings before assigning them. The attribute indexer reads/writes native attributes. SetClass and SetStyle mutate native state; the float SetStyle overload means **px**, so use a string ending in `dp` for scalable dimensions. Subscribe/On/OnClick attach events by ID. Markup replacement can replace children and their native handles; prefer updating leaf nodes.

`ObservableValue<T>`, `UIBinding<T>`, `UIBindingGroup`, and `RmlBindings` provide managed observable DOM updates. RmlBindings covers markup, progress values, pixel lengths, and classes. Dispose binding groups so subscriptions do not outlive the controller. This is not a reflection bridge for native RmlUi data models.

Native RmlUi supports data models, expressions/interpolation, data-for loops, conditional/visible views, class/style/attribute/text/RML/value/checked views, aliases, and event/value controllers. They require a registered C++ data model. PlutoGE does not register arbitrary C# properties as RmlUi models, so do not use `data-model` or `{{variable}}` expecting it to bind a C# field automatically. For game scripts use the managed DOM/observable API. Native extensions can register a model explicitly and dirty its variables; follow upstream structural-view restrictions.

## Events and engine-specific extensions

Native events cover pointer movement/buttons/wheel, click/double click, hover transitions, keyboard/text, focus/blur, document show/hide/resize/scroll, control change/submit, drag start/move/over/out/drop/end, and animation/transition notifications. Native listeners have capture and bubble phases. The managed API exposes callbacks without a general payload object; query state from stable elements. For drag/drop, PlutoGE records the stable dragged element ID in `data-drag-source`; `OnDrop(Action<string>)` reads and clears it.

### Pan/zoom scroll containers

An element with `data-pan-zoom="content-id"` opts into engine wheel zoom and right-button panning. Put the named content inside a single spacer, inside the scroll viewport. Set overflow:auto and give the content a known size. The engine transforms content around its top-left and resizes the spacer to preserve scroll bounds; data-zoom records the current value, bounded to 0.4–1.8. RmlElement.ScrollIntoView uses the pan/zoom-aware reveal helper. It projects pointer coordinates through transforms, so it works with scaled canvases and dp layout.

### Live render textures

RHI img sources and image decorators can reference RenderTexture assets so UI displays an offscreen camera output. Provide the correct project asset reference and explicit image dimensions. The RHI texture resolver shares GPU textures and performs required transitions; it does not create screenshot files. Test orientation, aspect ratio and camera-stack output. This is separate from a cached mesh portrait.

### Cached character/equipment portraits

```csharp
Span<uint> attachments = stackalloc uint[] { weaponId, armourId };
document.Element("portrait").SetScenePortrait(player.EntityId, attachments, ++revision, 320, 384);
```

Give the img explicit dimensions. Update revision when appearance changes. Engine attributes are `data-preview-root`, `data-preview-attachments`, `data-preview-revision`, `data-preview-width`, `data-preview-height`; diagnostics are `data-preview-render-count` and `data-preview-rendered-revision`. The engine installs a portrait:// texture on success. Zero attachment IDs are ignored. Rendering uses visible mesh hierarchies, root-relative placement, automatic framing, neutral light and transparency, without spawning gameplay entities. Bounds: 32–1024 texture dimensions, up to four simultaneous portraits and 32 detached roots. Hidden portraits release resources. This initial path is for static/rigid hierarchies; skinning snapshots/material overlay passes are not promised.

### World canvases, generated text, loading, and previews

WorldSpaceOverlay projects entity position to a constant-screen-size camera-facing overlay, useful for nameplates and health bars. WorldSpace creates a depth-tested texture plane following the entity transform, using RectTransform size/pivot and 100 UI units per world unit. Size modes include constant screen size and distance scaling. Documents are instanced per entity; shared source assets do not share DOM instances. The current legacy OpenGL host supports render-to-texture world surfaces; the RHI path should not be assumed to support every legacy world-surface feature. World contexts keep dp ratio 1.

Canvas Text content generates an in-memory document from the entity's UI Text component. Generated documents use the same screen/projected/world request path. Loaded scene documents receive the pluto-runtime class, so authored styles can distinguish them from isolated previews. The loading://active document target scopes managed mutations to the active loading controller; loading documents have independent noninteractive contexts/renderers. See [loading screens](scene-loading.md) when using that workflow.

The UI Document Editor supports unsaved-source offscreen previews, resolution/zoom controls, parser diagnostics, source undo/redo, external-file conflict handling and saved hot reload. Preview sources are temporary overlays and never become game assets automatically. Source files remain authoritative. Isolated previews retain dp ratio 1; use runtime tests/game viewports to inspect the player's interface scale.

## Rendering, performance, and validation

The shared RHI renderer runs on Vulkan/OpenGL with 2x supersampling per axis, transparent premultiplied composition and clipping. Very large outputs fall back to native resolution to bound allocations. The font raster patch increases atlas density for canvas transforms without changing logical advances/wrapping; dp scaling changes logical font size through layout instead. Avoid changing unchanged styles every frame or rebuilding large markup subtrees for a single numeric value. Cache managed wrappers and update changed state. Do not allocate/unload portraits per frame.

Before delivering a UI change:

The optional real CoD regression/capture command is `PlutoGERmlUiInterfaceScaleTests --cod <CoD-project-root> <output-directory>`. It checks migrated corner anchors, damage coverage, physical crosshair size, settings columns/sliders, and scrollable footer access at 0.75/1/1.25, and writes PPM captures without running gameplay.

1. Build ScriptCore and engine together after bridge/API changes, then build the game's scripts.
2. Validate XML, IDs, control bounds and script binding IDs.
3. Check 0.75, 1, 1.25 and larger supported scales at narrow/short/wide viewports.
4. Check corner anchors, full-screen damage/death coverage and projected crosshair geometry.
5. Check pointer drag, slider value, keyboard focus, scrolling, hidden tabs and cursor/input capture.
6. Check world labels, hot reload, scene transitions and restarting play mode.
7. Run `PlutoGERmlUiInterfaceScaleTests` on Vulkan and `--opengl`; run font-raster and preview regressions when changing context/render behavior.

## Agent entry point and maintenance

For UI tasks, read this guide, [the pinned feature reference](RMLUI_REFERENCE.md), the target project's theme source/generator, and its controllers before editing. Use dp for scalable screen layout, keep viewport roots untransformed, preserve physical/projected geometry, and keep world UI independent. Do not infer browser features from familiar HTML/CSS spelling. Do not claim parser-only or backend-specific features work everywhere. Keep generated `.rcss` and `.rcss.in` consistent. Register new assets with project metadata/export tooling. Leave unrelated scene/assets/build configuration edits intact.

When updating RmlUi, regenerate the pinned property/instancer inventory, review the backend capability matrix, run interface-scale/preview/font tests, and update this guide. Upstream feature manuals provide exhaustive syntax: [RML](https://mikke89.github.io/RmlUiDoc/pages/rml.html), [RCSS](https://mikke89.github.io/RmlUiDoc/pages/rcss.html), [forms and generated controls](https://mikke89.github.io/RmlUiDoc/pages/style_guide.html), [native data bindings](https://mikke89.github.io/RmlUiDoc/pages/data_bindings.html). The local pinned inventory takes precedence over newer upstream additions.
