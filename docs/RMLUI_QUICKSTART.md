# RmlUi project quick start

This guide creates a simple screen-space RmlUi panel in a PlutoGE project.
RmlUi is already part of the engine build; each game project only needs its
UI assets, a Canvas component, and optionally a C# controller.

## 1. Create the UI assets

Create this layout inside the project:

```text
MyGame/
└── Assets/
    └── UI/
        ├── hello.rml
        ├── hello.rcss
        └── Fonts/
            └── MyFont.ttf
```

The Content Browser can create a paired RML/RCSS asset, or the files can be
created manually. Copy a licensed TrueType or OpenType font into `Fonts`.
RmlUi does not provide built-in Arial or other system fonts. PlutoGE reads the
`@font-face` rules in sibling RCSS files and registers those font files with
RmlUi before loading the document.

Put this in `hello.rml`:

```xml
<rml>
<head>
    <title>Hello UI</title>
    <link type="text/rcss" href="hello.rcss"/>
</head>
<body>
    <div id="panel">
        <h1 id="title">RmlUi is working</h1>
        <button id="continue-button">Continue</button>
    </div>
</body>
</rml>
```

Put this in `hello.rcss`:

```css
@font-face {
    font-family: GameUI;
    src: url("Fonts/MyFont.ttf");
}

body {
    width: 100%;
    height: 100%;
    margin: 0;
    color: #ffffff;
    font-family: GameUI;
}

#panel {
    position: absolute;
    left: 50%;
    top: 50%;
    width: 360px;
    margin-left: -180px;
    margin-top: -90px;
    padding: 24px;
    background-color: #101722ee;
    border: 2px #58c8ff;
}

h1 {
    margin: 0 0 18px 0;
    font-size: 28px;
}

button {
    width: 100%;
    height: 44px;
    color: #ffffff;
    background-color: #24374a;
    border: 1px #58c8ff;
    font-family: GameUI;
    font-size: 16px;
}

button:hover {
    background-color: #315672;
}
```

URLs in RML and RCSS are relative to the file containing the URL. Keep the
font and stylesheet paths consistent if the files are moved.

## 2. Add the Canvas

In the scene:

1. Create an entity, for example `Game UI`.
2. Add a **Canvas** component.
3. Add an **RML Widget** component to an entity.
4. Choose `hello.rml` from the component's **Source** dropdown.
5. Make sure the entity and Canvas are enabled.
6. Enter Play mode.

The widget stores a project asset reference selected by the editor. An ordinary
path such as `UI/hello.rml` remains supported and is relative to the project's
`Assets` directory. `project://UI/hello.rml` is also accepted. RmlUi widgets are
screen-space overlays and do not need child Text, Image, or Button components.

RML and sibling RCSS files hot reload while the document is active, so most
visual edits should appear after saving the file.

To scale the whole document with the viewport, set the Canvas **Scale Mode** to
**Scale With Screen Size** and choose a reference resolution (for example,
1920 x 1080). Pixel dimensions and text in the RML document then scale with the
Canvas, while its screen-match controls determine how aspect-ratio differences
are handled.

### Blurred backdrops

RmlUi's `backdrop-filter` operates on the rendered game scene behind an
element. Combine it with a translucent background so the blur remains visible:

```css
#backdrop {
    width: 100%;
    height: 100%;
    background-color: #05080d99;
    backdrop-filter: blur(8px);
}
```

Large blur radii and full-screen filtered elements cost more GPU time than
small local panels, so use the lowest radius that suits the design.

## 3. Add behaviour with C# (optional)

Elements should have stable `id` attributes when scripts need to access them.
The string passed to `RmlDocument` must match the Canvas document path.

```csharp
using PlutoGE.ScriptCore;

public sealed class HelloUiController : ScriptBehaviour
{
    private RmlWidgetComponent? _widget;
    private RmlElement? _title;

    public override void OnCreate()
    {
        _widget = GetComponent<RmlWidgetComponent>();
        if (_widget is null) return;
        _title = _widget.Element("title");
        _widget.OnClick("continue-button", Continue);
    }

    private void Continue()
    {
        if (_title is not null)
            _title.Markup = "Button clicked!";
    }

}
```

Save this under `Assets/Scripts`, build the project scripts from
**Runtime → Build Scripts**, and attach it as a Script component to an active
entity.

Useful managed operations include:

```csharp
document.Show();
document.Hide();
document.Toggle();
document.Disable(); // Hides and pauses callbacks.
document.Enable();  // Restores the requested visibility.
document.Reload();
element.Markup = "New contents";
element["disabled"] = "disabled";
element.SetClass("warning", true);
element.SetStyle("left", 24.0f);
element.OnClick(() => Debug.Log("Clicked"));
```

`RmlDocument` can be treated as a reusable widget. Its `Visible` and `Enabled`
properties can also be bound to controller state. The older
`Subscribe("click").Consume()` polling API remains available when explicit
event polling is preferable.

For entity-owned UI, prefer `RmlWidgetComponent`. Its `Source`, `Visible`, and
inherited `Enabled` properties can be changed by scripts:

```csharp
var widget = GetComponent<RmlWidgetComponent>();
if (widget is not null)
{
    widget.Visible = false;
    widget.Enabled = true;
}
```

## Troubleshooting

Look at the editor/runtime console first. A successful load reports:

```text
[RmlUi] Loaded Canvas document 'UI/hello.rml' from '...'
```

The runtime also reports the resolved path when a file is missing and reports
parse/load failures separately.

If nothing appears:

- Confirm the entity has an enabled RML Widget with a Source selected.
- Confirm the Canvas, its entity, and its parent entities are active.
- Confirm the path is beneath `Assets` and uses the correct capitalization.
- Confirm the RML has a `<body>` and the RCSS gives visible elements dimensions,
  colors, or backgrounds.
- Confirm the referenced font exists. Text will not render without a usable
  font face.
- Check the messages immediately before a parse/load failure for the RML or
  RCSS line that caused it.
- Ensure a C# `RmlDocument` uses exactly the same path spelling as the Canvas.

For a complete interactive example, see `samples/rmlui/pause-menu.rml`,
`samples/rmlui/pause-menu.rcss`, and
`PlutoGE.ScriptCore.Examples.RmlPauseMenuController`.
