using System.Numerics;

namespace PlutoGE.ScriptCore;

// Wrappers for the native UI components. Property names match the native
// serialized names (see the C++ components' Serialize functions).

public enum UIContainerMode { None, Flex, Grid }
public enum UIFlexDirection { Row, Column, RowReverse, ColumnReverse }
public enum UIFlexWrap { NoWrap, Wrap }
public enum UIJustifyContent { Start, Center, End, SpaceBetween, SpaceAround, SpaceEvenly }
public enum UIAlignItems { Start, Center, End, Stretch }
public enum UIAlignContent { Start, Center, End, Stretch, SpaceBetween, SpaceAround }
public enum UIAlignSelf { Auto, Start, Center, End, Stretch }
public enum UISizeMode { Fixed, Content }
public enum UISliderDirection { LeftToRight, RightToLeft, BottomToTop, TopToBottom }
public enum UIScrollMovement { Unrestricted, Elastic, Clamped }
public enum UIInputContentType { Standard, Integer, Decimal, Alphanumeric, Password }
public enum UINavigationMode { Automatic, Explicit, None }

/// <summary>Shared state of interactive controls.</summary>
public abstract class UISelectableComponent : RegisteredComponent
{
    protected UISelectableComponent(uint entityId) : base(entityId)
    {
    }

    public bool Interactable
    {
        get => GetBool("Interactable");
        set => SetBool("Interactable", value);
    }

    public UINavigationMode Navigation
    {
        get => GetEnum<UINavigationMode>("Navigation");
        set => SetEnum("Navigation", value);
    }

    public bool IsFocused => UI.Focused?.EntityId == EntityId;
    public void Focus() => UI.Focused = GameObject;
}

/// <summary>A box with a solid or gradient fill, rounded corners, a border and an optional shadow.</summary>
[NativeComponent("UIShapeComponent")]
public sealed class UIShapeComponent : RegisteredComponent
{
    internal UIShapeComponent(uint entityId) : base(entityId)
    {
    }

    public Vector4 Color { get => GetVector4("Color"); set => SetVector4("Color", value); }
    public Vector4 EndColor { get => GetVector4("EndColor"); set => SetVector4("EndColor", value); }
    public bool Gradient { get => GetInt("FillType") == 1; set => SetInt("FillType", value ? 1 : 0); }
    public float GradientAngle { get => GetFloat("GradientAngle"); set => SetFloat("GradientAngle", value); }
    /// <summary>Corner radii: top-left, top-right, bottom-right, bottom-left.</summary>
    public Vector4 CornerRadii { get => GetVector4("CornerRadii"); set => SetVector4("CornerRadii", value); }
    public float CornerRadius { set => CornerRadii = new Vector4(value); }
    public float BorderWidth { get => GetFloat("BorderWidth"); set => SetFloat("BorderWidth", value); }
    public Vector4 BorderColor { get => GetVector4("BorderColor"); set => SetVector4("BorderColor", value); }
    public bool Shadow { get => GetBool("Shadow"); set => SetBool("Shadow", value); }
    public Vector4 ShadowColor { get => GetVector4("ShadowColor"); set => SetVector4("ShadowColor", value); }
    public Vector2 ShadowOffset { get => GetVector2("ShadowOffset"); set => SetVector2("ShadowOffset", value); }
    public float ShadowBlur { get => GetFloat("ShadowBlur"); set => SetFloat("ShadowBlur", value); }
    public float ShadowSpread { get => GetFloat("ShadowSpread"); set => SetFloat("ShadowSpread", value); }
    public bool ShadowInset { get => GetBool("ShadowInset"); set => SetBool("ShadowInset", value); }
}

/// <summary>Arranges children as a flex row/column or a grid.</summary>
[NativeComponent("UILayoutComponent")]
public sealed class UILayoutComponent : RegisteredComponent
{
    internal UILayoutComponent(uint entityId) : base(entityId)
    {
    }

    public UIContainerMode Mode { get => GetEnum<UIContainerMode>("Mode"); set => SetEnum("Mode", value); }
    public UIFlexDirection Direction { get => GetEnum<UIFlexDirection>("Direction"); set => SetEnum("Direction", value); }
    public UIFlexWrap Wrap { get => GetEnum<UIFlexWrap>("Wrap"); set => SetEnum("Wrap", value); }
    public UIJustifyContent Justify { get => GetEnum<UIJustifyContent>("Justify"); set => SetEnum("Justify", value); }
    public UIAlignItems AlignItems { get => GetEnum<UIAlignItems>("AlignItems"); set => SetEnum("AlignItems", value); }
    public UIAlignContent AlignContent { get => GetEnum<UIAlignContent>("AlignContent"); set => SetEnum("AlignContent", value); }
    public UIAlignItems JustifyItems { get => GetEnum<UIAlignItems>("JustifyItems"); set => SetEnum("JustifyItems", value); }
    /// <summary>Padding: left, bottom, right, top.</summary>
    public Vector4 Padding { get => GetVector4("Padding"); set => SetVector4("Padding", value); }
    public Vector2 Gap { get => GetVector2("Gap"); set => SetVector2("Gap", value); }
    /// <summary>Grid columns such as "120 1fr auto".</summary>
    public string Columns { get => GetString("Columns"); set => SetString("Columns", value); }
    public string Rows { get => GetString("Rows"); set => SetString("Rows", value); }
}

/// <summary>Content sizing, flex grow/shrink and grid placement for one element.</summary>
[NativeComponent("UILayoutItemComponent")]
public sealed class UILayoutItemComponent : RegisteredComponent
{
    internal UILayoutItemComponent(uint entityId) : base(entityId)
    {
    }

    public UISizeMode WidthMode { get => GetEnum<UISizeMode>("WidthMode"); set => SetEnum("WidthMode", value); }
    public UISizeMode HeightMode { get => GetEnum<UISizeMode>("HeightMode"); set => SetEnum("HeightMode", value); }
    public float Grow { get => GetFloat("Grow"); set => SetFloat("Grow", value); }
    public float Shrink { get => GetFloat("Shrink"); set => SetFloat("Shrink", value); }
    public float Basis { get => GetFloat("Basis"); set => SetFloat("Basis", value); }
    public UIAlignSelf AlignSelf { get => GetEnum<UIAlignSelf>("AlignSelf"); set => SetEnum("AlignSelf", value); }
    public float AspectRatio { get => GetFloat("AspectRatio"); set => SetFloat("AspectRatio", value); }
    /// <summary>Grid column and row; -1 places automatically.</summary>
    public Vector2 Cell { get => GetVector2("Cell"); set => SetVector2("Cell", value); }
    public Vector2 Span { get => GetVector2("Span"); set => SetVector2("Span", value); }
    public bool IgnoreLayout { get => GetBool("IgnoreLayout"); set => SetBool("IgnoreLayout", value); }
}

[NativeComponent("UIToggleComponent")]
public sealed class UIToggleComponent : UISelectableComponent
{
    internal UIToggleComponent(uint entityId) : base(entityId)
    {
    }

    public bool IsOn { get => GetBool("IsOn"); set => SetBool("IsOn", value); }
    public GameObject? Graphic { get => GetEntity("Graphic"); set => SetEntity("Graphic", value); }
    public GameObject? Group { get => GetEntity("Group"); set => SetEntity("Group", value); }

    public event Action<bool>? ValueChanged
    {
        add => UI.Subscribe(EntityId, UIEventType.ValueChanged, value, () => value!(IsOn));
        remove => UI.Unsubscribe(EntityId, UIEventType.ValueChanged, value);
    }
}

[NativeComponent("UIToggleGroupComponent")]
public sealed class UIToggleGroupComponent : RegisteredComponent
{
    internal UIToggleGroupComponent(uint entityId) : base(entityId)
    {
    }

    public bool AllowSwitchOff { get => GetBool("AllowSwitchOff"); set => SetBool("AllowSwitchOff", value); }
}

[NativeComponent("UISliderComponent")]
public sealed class UISliderComponent : UISelectableComponent
{
    internal UISliderComponent(uint entityId) : base(entityId)
    {
    }

    public float Value { get => GetFloat("Value"); set => SetFloat("Value", value); }
    public float Minimum { get => GetFloat("Minimum"); set => SetFloat("Minimum", value); }
    public float Maximum { get => GetFloat("Maximum", 1.0f); set => SetFloat("Maximum", value); }
    public bool WholeNumbers { get => GetBool("WholeNumbers"); set => SetBool("WholeNumbers", value); }
    public UISliderDirection Direction { get => GetEnum<UISliderDirection>("Direction"); set => SetEnum("Direction", value); }
    public GameObject? Fill { get => GetEntity("Fill"); set => SetEntity("Fill", value); }
    public GameObject? Handle { get => GetEntity("Handle"); set => SetEntity("Handle", value); }

    public float NormalizedValue
    {
        get
        {
            var range = Maximum - Minimum;
            return MathF.Abs(range) < 1e-6f ? 0.0f : Math.Clamp((Value - Minimum) / range, 0.0f, 1.0f);
        }
        set => Value = Minimum + Math.Clamp(value, 0.0f, 1.0f) * (Maximum - Minimum);
    }

    public event Action<float>? ValueChanged
    {
        add => UI.Subscribe(EntityId, UIEventType.ValueChanged, value, () => value!(Value));
        remove => UI.Unsubscribe(EntityId, UIEventType.ValueChanged, value);
    }
}

[NativeComponent("UIScrollViewComponent")]
public sealed class UIScrollViewComponent : RegisteredComponent
{
    internal UIScrollViewComponent(uint entityId) : base(entityId)
    {
    }

    public GameObject? Content { get => GetEntity("Content"); set => SetEntity("Content", value); }
    public bool Horizontal { get => GetBool("Horizontal"); set => SetBool("Horizontal", value); }
    public bool Vertical { get => GetBool("Vertical"); set => SetBool("Vertical", value); }
    public UIScrollMovement Movement { get => GetEnum<UIScrollMovement>("Movement"); set => SetEnum("Movement", value); }
    public float ScrollSensitivity { get => GetFloat("ScrollSensitivity"); set => SetFloat("ScrollSensitivity", value); }
    /// <summary>Scroll offset in canvas units: x to the right, y downwards.</summary>
    public Vector2 ScrollPosition { get => GetVector2("ScrollPosition"); set => SetVector2("ScrollPosition", value); }
    /// <summary>0 at the top/left, 1 at the bottom/right.</summary>
    public Vector2 NormalizedPosition { get => GetVector2("NormalizedPosition"); set => SetVector2("NormalizedPosition", value); }
    public void ScrollToTop() => NormalizedPosition = new Vector2(NormalizedPosition.X, 0.0f);
    public void ScrollToBottom() => NormalizedPosition = new Vector2(NormalizedPosition.X, 1.0f);
}

[NativeComponent("UIInputFieldComponent")]
public sealed class UIInputFieldComponent : UISelectableComponent
{
    internal UIInputFieldComponent(uint entityId) : base(entityId)
    {
    }

    public string Text { get => GetString("Text"); set => SetString("Text", value); }
    public GameObject? Placeholder { get => GetEntity("Placeholder"); set => SetEntity("Placeholder", value); }
    public int CharacterLimit { get => GetInt("CharacterLimit"); set => SetInt("CharacterLimit", value); }
    public UIInputContentType ContentType { get => GetEnum<UIInputContentType>("ContentType"); set => SetEnum("ContentType", value); }
    public bool Multiline { get => GetBool("Multiline"); set => SetBool("Multiline", value); }
    public bool ReadOnly { get => GetBool("ReadOnly"); set => SetBool("ReadOnly", value); }
    public bool IsEditing => GetBool("IsEditing");

    /// <summary>Raised for every edit.</summary>
    public event Action<string>? ValueChanged
    {
        add => UI.Subscribe(EntityId, UIEventType.ValueChanged, value, () => value!(Text));
        remove => UI.Unsubscribe(EntityId, UIEventType.ValueChanged, value);
    }

    /// <summary>Raised when editing finishes (Enter, focus moving away, or Escape after reverting).</summary>
    public event Action<string>? EndEdit
    {
        add => UI.Subscribe(EntityId, UIEventType.EndEdit, value, () => value!(Text));
        remove => UI.Unsubscribe(EntityId, UIEventType.EndEdit, value);
    }
}

[NativeComponent("UIDropdownComponent")]
public sealed class UIDropdownComponent : UISelectableComponent
{
    internal UIDropdownComponent(uint entityId) : base(entityId)
    {
    }

    public string[] Options
    {
        get
        {
            var text = GetString("Options");
            return text.Length == 0 ? [] : text.Split('\n');
        }
        set => SetString("Options", string.Join("\n", value ?? []));
    }

    public int Value { get => GetInt("Value"); set => SetInt("Value", value); }
    public string? SelectedOption => Value >= 0 && Value < Options.Length ? Options[Value] : null;
    public GameObject? Caption { get => GetEntity("Caption"); set => SetEntity("Caption", value); }

    public event Action<int>? ValueChanged
    {
        add => UI.Subscribe(EntityId, UIEventType.ValueChanged, value, () => value!(Value));
        remove => UI.Unsubscribe(EntityId, UIEventType.ValueChanged, value);
    }
}

/// <summary>
/// The theme for the UI under this entity (usually the canvas): a theme asset plus local styles that extend it.
/// Assigning <see cref="Theme"/> re-themes everything below, e.g. to switch between light and dark themes.
/// </summary>
[NativeComponent("UIStyleSheetComponent")]
public sealed class UIThemeComponent : RegisteredComponent
{
    internal UIThemeComponent(uint entityId) : base(entityId)
    {
    }

    /// <summary>Asset reference of the theme; a <see cref="UIThemeAsset"/> field converts to it.</summary>
    public string Theme { get => GetString("Theme"); set => SetString("Theme", value); }

    /// <summary>Local styles in theme text, layered over <see cref="Theme"/>.</summary>
    public string LocalStyles { get => GetString("Rules"); set => SetString("Rules", value); }
}

/// <summary>The styles an element uses from the nearest theme, applied in order (later ones win).</summary>
[NativeComponent("UIStyleComponent")]
public sealed class UIStyleComponent : RegisteredComponent
{
    internal UIStyleComponent(uint entityId) : base(entityId)
    {
    }

    /// <summary>Space-separated style names, e.g. "Button Primary-Button".</summary>
    public string Styles { get => GetString("Styles"); set => SetString("Styles", value); }

    public bool HasStyle(string name) => Split(Styles).Contains(name);

    public void AddStyle(string name)
    {
        if (!HasStyle(name))
            Styles = (Styles + " " + name).Trim();
    }

    public void RemoveStyle(string name) => Styles = string.Join(" ", Split(Styles).Where(item => item != name));

    /// <summary>Adds or removes a style, e.g. to mark an answer as correct.</summary>
    public void ToggleStyle(string name, bool on)
    {
        if (on) AddStyle(name);
        else RemoveStyle(name);
    }

    /// <summary>Replaces every style with one, e.g. switching a button between "Button" and "Primary-Button".</summary>
    public void SetStyle(string name) => Styles = name;

    /// <summary>"Component.Property" keys the element keeps its own value for instead of the style's.</summary>
    public string Overrides { get => GetString("Overrides"); set => SetString("Overrides", value); }

    [Obsolete("Use Styles.")] public string Classes { get => Styles; set => Styles = value; }
    [Obsolete("Use HasStyle.")] public bool HasClass(string name) => HasStyle(name);
    [Obsolete("Use AddStyle.")] public void AddClass(string name) => AddStyle(name);
    [Obsolete("Use RemoveStyle.")] public void RemoveClass(string name) => RemoveStyle(name);
    [Obsolete("Use ToggleStyle.")] public void ToggleClass(string name, bool on) => ToggleStyle(name, on);

    private static string[] Split(string list) => list.Split(' ', StringSplitOptions.RemoveEmptyEntries);
}
