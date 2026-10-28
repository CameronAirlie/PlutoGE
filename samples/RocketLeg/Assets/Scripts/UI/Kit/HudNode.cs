using System;
using System.Numerics;
using PlutoGE.ScriptCore;

namespace RocketLeg.Scripts.UI;

/// <summary>
/// One element of a code-built native UI: an entity with a RectTransform plus
/// whichever of text, shape, image and style it was given. Views build their
/// tree once with the fluent methods and then drive it through the cached
/// setters, which only cross into the engine when a value changes, so a view
/// can write its whole state every frame.
/// <para>
/// Looks come from <c>UI/Themes/RocketLeg.plutotheme</c> via style names; code
/// sets structure, placement and content. UI space is y-up: anchor (0,0) is
/// the parent's bottom-left corner and (1,1) its top-right.
/// </para>
/// </summary>
public sealed class HudNode
{
    private UITextComponent? _text;
    private UIImageComponent? _image;
    private UIStyleComponent? _style;
    private string? _textValue;
    private string? _styles;
    private bool? _visible;
    private float? _amount;
    private float? _opacity;
    private Vector2? _position;
    private Vector2? _scale;

    private HudNode(GameObject entity)
    {
        Object = entity;
        Rect = entity.GetComponent<RectTransformComponent>() ?? entity.AddComponent<RectTransformComponent>()
            ?? throw new InvalidOperationException($"{entity.Name} cannot take a RectTransform");
        // HUD graphics never take the pointer from gameplay.
        Rect.RaycastTarget = false;
    }

    public GameObject Object { get; }
    public RectTransformComponent Rect { get; }

    public static HudNode Wrap(GameObject entity) => new(entity);

    /// <summary>A new child element, painted over its earlier siblings.</summary>
    public HudNode Add(string name) =>
        new(GameObject.Create(name, Object) ?? throw new InvalidOperationException($"Could not create UI element {name}"));

    // ---- Placement -------------------------------------------------------------

    /// <summary>
    /// A fixed-size element at <paramref name="anchor"/>, inset from the edges
    /// the anchor touches (from the centre otherwise; +x right, +y up). The pivot
    /// follows the anchor, so <c>Pin(Anchors.TopRight, (24, 24), size)</c> sits
    /// 24 units in from the top-right corner.
    /// </summary>
    public HudNode Pin(Vector2 anchor, Vector2 inset, Vector2 size)
    {
        Rect.Place(anchor, anchor, anchor, Inward(anchor, inset), size);
        _position = null;
        return this;
    }

    /// <summary>Centred on <paramref name="anchor"/> and offset by <paramref name="offset"/>.</summary>
    public HudNode Centre(Vector2 anchor, Vector2 offset, Vector2 size)
    {
        Rect.Place(anchor, anchor, new Vector2(0.5f), offset, size);
        _position = null;
        return this;
    }

    /// <summary>Stretched over the parent, inset from each edge.</summary>
    public HudNode Fill(float left = 0, float bottom = 0, float right = 0, float top = 0)
    {
        Rect.Place(Vector2.Zero, Vector2.One, new Vector2(0.5f), new Vector2((left - right) * 0.5f, (bottom - top) * 0.5f),
                   new Vector2(-(left + right), -(bottom + top)));
        _position = null;
        return this;
    }

    // ---- Components ------------------------------------------------------------

    public HudNode Style(string styles)
    {
        Styles = styles;
        return this;
    }

    /// <summary>A box whose fill, corners, border and shadow come from its style.</summary>
    public HudNode Shape(string styles)
    {
        if (Object.GetComponent<UIShapeComponent>() is null) Object.AddComponent<UIShapeComponent>();
        Styles = styles;
        return this;
    }

    /// <summary>A procedural or filled image; filled types draw <see cref="Amount"/> of it.</summary>
    public HudNode Image(UIImageType type, string styles, float startAngle = 90.0f, bool clockwise = true)
    {
        _image ??= Object.GetComponent<UIImageComponent>() ?? Object.AddComponent<UIImageComponent>();
        _image!.ImageType = type;
        _image.StartAngle = startAngle;
        _image.FillClockwise = clockwise;
        Styles = styles;
        return this;
    }

    public HudNode Label(string text, string styles, UITextAlignment alignment = UITextAlignment.MiddleCenter)
    {
        _text ??= Object.GetComponent<UITextComponent>() ?? Object.AddComponent<UITextComponent>();
        _text!.Alignment = alignment;
        _text.RichText = true;
        Text = text;
        Styles = styles;
        return this;
    }

    public HudNode Wrapping()
    {
        TextComponent.Wrap = true;
        return this;
    }

    public UITextComponent TextComponent =>
        _text ??= Object.GetComponent<UITextComponent>() ?? throw new InvalidOperationException($"{Object.Name} has no text");
    public UIImageComponent ImageComponent =>
        _image ??= Object.GetComponent<UIImageComponent>() ?? throw new InvalidOperationException($"{Object.Name} has no image");

    // ---- Cached state ----------------------------------------------------------

    /// <summary>Hidden elements are skipped by layout and paint.</summary>
    public bool Visible
    {
        get => _visible ?? Object.Active;
        set
        {
            if (_visible == value) return;
            _visible = value;
            Object.Active = value;
        }
    }

    public string Text
    {
        get => _textValue ?? TextComponent.Text;
        set
        {
            value ??= string.Empty;
            if (_textValue == value) return;
            _textValue = value;
            TextComponent.Text = value;
        }
    }

    /// <summary>Theme styles, space separated (later ones win).</summary>
    public string Styles
    {
        get => _styles ?? string.Empty;
        set
        {
            value ??= string.Empty;
            if (_styles == value) return;
            _styles = value;
            _style ??= Object.GetComponent<UIStyleComponent>() ?? Object.AddComponent<UIStyleComponent>();
            _style!.Styles = value;
        }
    }

    /// <summary>The drawn share of a filled or arc image, 0..1.</summary>
    public float Amount
    {
        get => _amount ?? ImageComponent.FillAmount;
        set
        {
            value = Math.Clamp(value, 0.0f, 1.0f);
            if (_amount is { } last && MathF.Abs(last - value) < 0.0005f) return;
            _amount = value;
            ImageComponent.FillAmount = value;
        }
    }

    public float Opacity
    {
        get => _opacity ?? Rect.Opacity;
        set
        {
            value = Math.Clamp(value, 0.0f, 1.0f);
            if (_opacity is { } last && MathF.Abs(last - value) < 0.002f) return;
            _opacity = value;
            Rect.Opacity = value;
        }
    }

    /// <summary>Offset from the anchor (y-up).</summary>
    public Vector2 Position
    {
        get => _position ?? Rect.AnchoredPosition;
        set
        {
            if (_position is { } last && Vector2.DistanceSquared(last, value) < 0.0025f) return;
            _position = value;
            Rect.AnchoredPosition = value;
        }
    }

    /// <summary>Scale about the pivot, for pop-in animations.</summary>
    public float Scale
    {
        set
        {
            var scale = new Vector2(value);
            if (_scale is { } last && Vector2.DistanceSquared(last, scale) < 1e-6f) return;
            _scale = scale;
            Rect.LocalScale = scale;
        }
    }

    private static Vector2 Inward(Vector2 anchor, Vector2 inset) =>
        new(anchor.X >= 0.999f ? -inset.X : inset.X, anchor.Y >= 0.999f ? -inset.Y : inset.Y);
}

/// <summary>Anchor points in UI space (y-up).</summary>
public static class Anchors
{
    public static readonly Vector2 TopLeft = new(0, 1), Top = new(0.5f, 1), TopRight = new(1, 1);
    public static readonly Vector2 Left = new(0, 0.5f), Middle = new(0.5f, 0.5f), Right = new(1, 0.5f);
    public static readonly Vector2 BottomLeft = new(0, 0), Bottom = new(0.5f, 0), BottomRight = new(1, 0);
}
