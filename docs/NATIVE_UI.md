# Native UI

The native UI builds interfaces from scene entities. Every element is an entity
with a `RectTransformComponent`; parenting in the scene hierarchy is UI nesting,
sibling order is paint and layout order, the active flag shows and hides
elements, and prefabs are reusable widgets. It renders on both RHI backends
(Vulkan and OpenGL) and coexists with [RmlUi](RMLUI_INTEGRATION.md): a
`CanvasComponent` chooses its backend, and native canvases draw beneath RmlUi
documents.

Use the native UI for game HUDs, menus and in-world labels that you want to
build in the editor and drive from C#. Use RmlUi for document-style screens
written in RML/RCSS.

## Quick start

1. In the hierarchy, **Create > UI > Button** (any widget creates a canvas if
   none is above it).
2. Open **Canvas Editor** (the UI designer) to see and arrange it.
3. Script it:

```csharp
public sealed class MainMenu : ScriptBehaviour
{
    [SerializedField] private GameObject? PlayButton;
    [SerializedField] private GameObject? Volume;

    public override void OnCreate()
    {
        PlayButton?.GetComponent<UIButtonComponent>()!.Clicked += () => SceneManager.Load("project://Level1.plutoscene");
        Volume?.GetComponent<UISliderComponent>()!.ValueChanged += value => AudioSettings.MasterVolume = value;
    }
}
```

## Building blocks

| Component | Purpose |
| --- | --- |
| `CanvasComponent` (Native) | Root of a UI tree. Scale mode (constant pixels, scale with screen size against a reference resolution, physical), sorting order, and render mode (screen space, or world space billboards). |
| `RectTransformComponent` | Position and size: anchors, pivot, anchored position, size delta, margin, min/max size, rotation, scale, opacity, clip children, raycast target. |
| `UILayoutComponent` | Arranges children: **Flex** (row/column, wrap, justify, align items/content, gap, padding) or **Grid** (columns/rows as `120 1fr auto`). |
| `UILayoutItemComponent` | Per-child options: width/height *Fixed* or *Content* (fit text or children), grow, shrink, basis, align self, aspect ratio, grid cell/span, ignore layout. |
| `UIShapeComponent` | Box drawing: solid or linear-gradient fill, per-corner radii, border, drop or inset shadow. Its radii also round child clipping. |
| `UIImageComponent` | Textures: simple, 9-sliced, tiled, horizontal/vertical/radial fill, preserve aspect, rounded corners; plus procedural circle, arc, crosshair and rounded rect. |
| `UITextComponent` | SDF text: rich text markup, wrapping, alignment, overflow (overflow, clip, ellipsis), letter spacing, outline, shadow, padding. |
| `UIButtonComponent` | Clickable control with tint transitions. |
| `UIToggleComponent`, `UIToggleGroupComponent` | Check boxes and radio groups. |
| `UISliderComponent` | Draggable value with fill and handle elements. |
| `UIScrollViewComponent` | Scrolls a content element with the wheel or by dragging; inertia; elastic, clamped or unrestricted. |
| `UIInputFieldComponent` | Text entry: selection, clipboard, filters (integer, decimal, alphanumeric, password), character limit, multiline, placeholder. |
| `UIDropdownComponent` | Picks from a list in a popup. |
| `UIThemeComponent`, `UIStyleComponent` | Themes and styles: shared, inheritable looks (see below). |

Rich text tags: `<b> <i> <u> <s> <color=#RRGGBB[AA]> <size=N> <font=ref> <br>`.

### Layout model

Without a layout component, children are placed by their anchors relative to
the parent rect (RectTransform semantics). A `UILayoutComponent` places its
children with flexbox or grid rules instead; children with
`UILayoutItem.IgnoreLayout` keep their anchors. Margins are outer spacing in
both modes. Stretch (align items) and grow are explicit: an item keeps its
authored size unless you ask the container to change it. *Content* sizing
measures text (wrapping at the width it is given), images (texture size) and
nested layouts, so a column of wrapped labels can size a panel.

The RectTransform's own legacy layout-group fields (horizontal, vertical, grid
with "control/expand child size") still work and are converted to flex/grid.
New UI should use `UILayoutComponent`.

### Input and navigation

Pointer events bubble from the element under the pointer to its ancestors, so
a click on a button's label clicks the button. Controls respond to pointer,
wheel, drag, keyboard and gamepad:

- **Navigation**: arrows, Tab/Shift+Tab, d-pad and left stick move focus to the
  nearest control in that direction. Set a control's *Navigation* to
  *Explicit* to choose its neighbours, or *None* to skip it.
- **Submit / Cancel**: Enter or gamepad A clicks the focused control; Escape or
  gamepad B cancels (an input field reverts, a dropdown closes).
- **Gameplay input**: while interactive UI has the pointer, or a text field is
  being edited, script pointer/keyboard queries ignore it. Static HUD graphics
  do not capture the pointer.

### Styles and themes

A **style** is a named look, such as *Button*, *Heading* or *Panel*: a set of
component property values (shape colour, corner radii, font size...).
A **theme** holds styles and **variables**, which are named values like the
accent colour that styles share. Elements use styles from the nearest theme
above them, so changing the theme re-themes the whole UI.

1. Put a **UI Theme** component on the canvas and pick a theme asset. The
   **+** button creates one with ready-made styles: Panel, Label, Heading,
   Caption, Button, Primary/Danger Button and Input.
2. Give elements a **UI Style** component and choose a style. Widgets created
   from **Create > UI** pick up a matching style automatically.
3. Edit the theme in the **UI Theme Editor** (Window menu, double-click a
   `.plutotheme`, or the palette button next to a style). Changes show live.

Styles can be **based on** another style: *Primary-Button* based on *Button*
inherits everything Button sets and changes only the colour. Every style has
values for **states**: *Normal* always applies, and *Hover*, *Pressed* and
*Selected* layer on top while active. *Disabled* replaces Hover and Pressed.
A state value set anywhere in the chain wins over a Normal value. States come
from the nearest control at or above the element, and a style value for a
component the element lacks goes to the first child that has it, so a button
style can colour its label.

Themes can be based on other themes, for example a *Dark* theme based on
*Base* that overrides a few variables. A UI Theme component can also add
**local styles** that extend or override its theme for that part of the UI.
Theme components further down layer over the ones above.

In the inspector, values a style controls are marked with a blue bar and are
read-only. Right-click one to **Override on This Element** (marked orange,
and the element keeps its own value) or to edit the style. **Create Style from
Element** saves an element's current look as a new style. When a style stops
applying, because it was removed or a state ended, the element gets its own
value back.

Theme files are text, editable in the editor's **Source** tab:

```
@import "project://UI/Themes/Base.plutotheme"
@accent: color = 0.26,0.52,0.96,1
@radius: number = 10

.Button {
    UIShapeComponent.Color = @accent
    UIShapeComponent.CornerRadii = @radius,@radius,@radius,@radius
    :hover { UIShapeComponent.Color = 0.4,0.62,1,1 }
    :pressed { UIShapeComponent.Color = 0.18,0.4,0.82,1 }
}
.Danger-Button extends .Button { UIShapeComponent.Color = 0.9,0.25,0.25,1 }
```

Values are `ComponentType.Property = value`, using the serialized property
names and value text. Quote values containing `; { } #` or line breaks.
Style sheets written before themes, with class rules like `.button:hover`,
still load. Combined selectors (`.button.danger`) are no longer supported;
use `extends` instead.

### Animation

Sequencer tracks can animate any numeric component property: choose the
**Component Property** channel and a property such as
`UIShapeComponent.Color` or `RectTransformComponent.AnchoredPosition`. This
works for every component, not only UI.

## Editor

- **Create > UI** in the hierarchy builds complete widgets (button, toggle,
  slider, input field, dropdown, scroll view, rows, columns and grids) with
  their references wired.
- **Canvas Editor** (UI designer) renders the canvas at a chosen resolution:
  select, move, resize, rotate, re-pivot and re-anchor elements; smart guides
  snap to the parent and siblings; overlays show element outlines, layout
  padding and child slots, and the safe area; the palette adds widgets under
  the selection; the align buttons anchor the selection within its parent.
  Edits are undoable.
- The inspector has an anchor-preset grid, mode-aware layout fields and a
  multiline text editor with an alignment grid. Style-driven properties are
  marked, with override and revert actions.
- **UI Theme Editor**: a style tree arranged by inheritance (drag a style onto
  another to base it on that one), per-state values with typed controls,
  inherited values shown with their source, variables, base themes, a text
  view, and undo/redo. Asset themes save to their file (Ctrl+S); local
  styles save with the scene.

## Scripting

Native UI components are C# classes in `PlutoGE.ScriptCore`. Get them with
`GameObject.GetComponent<T>()`, add them with `AddComponent<T>()`.

| Type | Highlights |
| --- | --- |
| `UIButtonComponent` | `Clicked`, `Pressed`, `Released`, `PointerEntered/Exited`, `Interactable` |
| `UIToggleComponent` | `IsOn`, `ValueChanged(bool)` |
| `UISliderComponent` | `Value`, `Minimum`, `Maximum`, `NormalizedValue`, `ValueChanged(float)` |
| `UIInputFieldComponent` | `Text`, `ContentType`, `ValueChanged(string)`, `EndEdit(string)`, `IsEditing` |
| `UIDropdownComponent` | `Options`, `Value`, `SelectedOption`, `ValueChanged(int)` |
| `UIScrollViewComponent` | `ScrollPosition`, `NormalizedPosition`, `ScrollToTop/Bottom()` |
| `UIShapeComponent`, `UILayoutComponent`, `UILayoutItemComponent` | Their properties |
| `UIThemeComponent`, `UIStyleComponent` | `Theme` (assign a `UIThemeAsset` to switch themes), `LocalStyles`; `Styles`, `SetStyle`, `AddStyle`, `RemoveStyle`, `ToggleStyle`, `Overrides` |
| `UI` | `Focused`, `IsPointerOverUI`, `IsKeyboardCaptured`, `EventRaised` |

Data binding keeps controls and values in step:

```csharp
private readonly ObservableValue<float> _volume = new(0.8f);
private readonly ObservableValue<int> _score = new();
private readonly UIBindingGroup _bindings = new();

public override void OnCreate()
{
    _bindings.Add(UIBind.Value(VolumeSlider!, _volume));                 // two-way
    _bindings.Add(UIBind.Format(ScoreText!, _score, "Score: {0}"));      // one-way, formatted
}
public override void OnDestroy() => _bindings.Dispose();
```

`UIRepeater<T>` instantiates and reuses item views for lists.

UI can also be built entirely from code. `GameObject.Create(name, parent)` makes an
entity (last among its siblings), `AddComponent<T>()` adds any native component,
including the typed `RectTransformComponent`, `UITextComponent`,
`UIImageComponent`, `UIButtonComponent` and `CanvasComponent`, and
`Parent`/`SetParent(parent, siblingIndex)`, `SetSiblingIndex`, `Children` and
`FindChild("Panel/Title")` walk and rearrange the hierarchy:

```csharp
var panel = GameObject.Create("Panel", canvas)!;
var rect = panel.AddComponent<RectTransformComponent>()!;
rect.Place(anchorMin: new(0, 1), anchorMax: new(0, 1), pivot: new(0, 1),
           anchoredPosition: new(24, -24), sizeDelta: new(300, 40)); // y-up
var label = panel.AddComponent<UITextComponent>()!;
label.Text = "<b>READY</b>";
label.RichText = true;
panel.AddComponent<UIStyleComponent>()!.Styles = "Heading";
```

Beyond their dedicated calls, the typed wrappers expose the rest of their
serialized properties: `RectTransformComponent` (`AnchorMin`, `AnchorMax`,
`Pivot`, `Margin`, `MinimumSize`, `MaximumSize`, `ClipChildren`,
`RaycastTarget`), `UITextComponent` (`Rgba`, `FontPath`, `RichText`, `Wrap`,
`Overflow`, `LetterSpacing`, `LineSpacing`, `Bold`, `Italic`, outline, shadow,
`Padding`), `UIImageComponent` (`Rgba`, `CornerRadius`, `StartAngle`,
`FillClockwise`) and `CanvasComponent` (`Backend`, `RenderMode`,
`WorldSizeMode`, `MatchWidthOrHeight`).

Any registered component can also be reached by property name:
`component.GetProperty("Color")` / `SetProperty("Color", "1,0,0,1")` on a
`RegisteredComponent`.

## Architecture

```
engine/ui (PlutoGE::UI)     layout/ Flex+Grid solver   text/ fonts, SDF atlas, shaping, layout, rich text
                            render/ DrawList, Painter (boxes, shadows, images, rings, text)
engine/scene                components (UIComponent.h and components/UI*.h)
                            UISystem: collect hierarchy -> layout -> transforms/clips -> paint | input -> events
                            ui/ interfaces: IUIGraphic, IUIContentSize, IUIEventHandler, IUIRectDriver,
                                IUIUpdatable, IUIPopup, IUITextInput, UISelectable
                            ui/ themes: UITheme (model), UIThemeFormat (text), UIThemeResolver
                                (inheritance, variables, layers), UIThemeAssets (cache, live edits),
                                UIStyleCatalog (styleable properties from the registry)
engine/render               UIRhiRenderer (NativeUI.slang), NativeUIRenderer (frame integration),
                            NativeUIPreview (tools)
engine/scripting            ComponentScriptApi: generic component property and UI event bridge
editor/ui                   CanvasEditorPanel (designer), UIWidgetPresets, ComponentInspectors
                            theme/ UIThemeDocument (undo, storage), views; UIThemeEditorPanel
```

- **`PlutoGE::UI`** depends only on glm and FreeType and is unit tested on its
  own. Glyphs are rasterized once as signed distance fields (built from a
  coverage bitmap, which handles fonts with overlapping contours) into shared
  RGBA8 atlas pages, so text stays sharp at any scale.
  Text shaping is behind `ui::ITextShaper`; the default maps codepoints to
  glyphs with kerning and font fallback, and a HarfBuzz shaper can replace it
  for complex scripts without other changes.
- **Rendering**: one shader evaluates rounded-rect distance fields (fill,
  border, soft shadow, inverted inset shadow), glyph distance fields (weight,
  outline, soft shadow) and a rounded clip per pixel. Geometry is batched by
  texture and scissor, retained per canvas and uploaded only when it changes.
- **Extending**: a new visual is a component implementing `IUIGraphic` (and
  `IUIContentSize` to size to content); a new control implements
  `UISelectable` (or `IUIEventHandler`), and `IUIRectDriver` if it positions
  other elements. Register it in `UIComponentTypes.cpp`
  (`ComponentRegistry`); serialization, prefabs, the Add Component menu,
  property animation, styles (including the theme editor's property lists)
  and the C# property bridge then work without
  further changes. Add a C# wrapper class with `[NativeComponent("TypeName")]`
  for typed scripting, and optionally a custom inspector in
  `UIComponentInspectors.cpp` and a widget preset in `UIWidgetPresets.cpp`.

### Frame order

`Scene::Update` runs the UI before components: rect drivers and styles, layout,
pointer/navigation/text input, then per-frame control updates (scroll inertia,
caret blink, tint transitions). Scripts then receive the frame's UI events.
Rendering lays the canvases out for the target size and draws them after
post-processing, below RmlUi.

## Limitations

- Nested rounded clips: the innermost rounded clip is exact; outer ones clip
  to their bounds. Rotated rounded clips also use their bounds.
- World-space canvases are camera-facing billboards drawn over the scene; they
  are not depth-tested yet.
- Backdrop blur, sprite masks and complex-script shaping (HarfBuzz) are not
  implemented.
- Styles apply instantly; state transitions animate only button tints.
- Renaming a style updates elements in the open scene only; other scenes keep
  the old name. Editing a theme visually drops comments in its text.

## Tests

| Test | Covers |
| --- | --- |
| `PlutoGENativeUICoreTests` | UTF-8, rich text, SDF atlas, line breaking, alignment, ellipsis, draw lists, painter |
| `PlutoGENativeUILayoutTests` | Anchors, flex (grow/shrink/wrap/justify/align), grid, content sizing |
| `PlutoGEVulkanNativeUIRender`, `PlutoGEOpenGLNativeUIRender` | GPU output: shapes, blending, borders, clips, gradients, images, glyphs, deferred replay |
| `PlutoGENativeUISceneTests` | Hierarchy layout, legacy layout groups, transforms, clipping, canvas order, buttons, property animation, themes and styles on elements |
| `PlutoGENativeUIThemeTests` | Theme model edits, text format, inheritance, variables, layering, error reporting, style catalog |
| `PlutoGEUIThemeEditorTests` | Theme documents (undo, saving, routed edits) and the editor views in a hidden window; pass a path prefix to capture images |
| `PlutoGENativeUIControlsTests` | Toggles, sliders, scroll views, input fields, dropdowns, navigation, pointer capture |
| `PlutoGENativeUIScriptingTests` | C# components, properties, events and bindings through the managed runtime; building a hierarchy from script |
| `PlutoGENativeUIEditorTests` | Widget presets and inspector registration |
| `PlutoGEComponentRegistryTests` | Component registration, creation and authoring rules |
