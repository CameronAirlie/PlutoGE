using System.Numerics;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

public enum UITextAlignment
{
    TopLeft, TopCenter, TopRight, MiddleLeft, MiddleCenter,
    MiddleRight, BottomLeft, BottomCenter, BottomRight
}

public sealed class UITextComponent : ComponentReference
{
    internal UITextComponent(uint entityId) : base(entityId) {}

    internal override ScriptBridge.NativeComponentType ComponentType => ScriptBridge.NativeComponentType.UIText;

    public string Text
    {
        get => ScriptBridge.GetUIText(EntityId);
        set => ScriptBridge.SetUIText(EntityId, value);
    }

    public Vector3 Color
    {
        get => ScriptBridge.GetUITextColor(EntityId);
        set => ScriptBridge.SetUITextColor(EntityId, value);
    }

    public float FontSize
    {
        get => ScriptBridge.GetUITextFontSize(EntityId);
        set => ScriptBridge.SetUITextFontSize(EntityId, value);
    }

    public UITextAlignment Alignment
    {
        get => (UITextAlignment)ScriptBridge.GetUITextAlignment(EntityId);
        set => ScriptBridge.SetUITextAlignment(EntityId, (int)value);
    }

    // Through the generic property bridge.

    private const string Type = "UITextComponent";

    /// <summary>Colour with alpha.</summary>
    public Vector4 Rgba { get => ComponentProperties.GetVector4(EntityId, Type, "Color"); set => ComponentProperties.SetVector4(EntityId, Type, "Color", value); }

    /// <summary>Font asset reference (empty for the default UI font).</summary>
    public string FontPath { get => ComponentProperties.GetString(EntityId, Type, "FontPath"); set => ComponentProperties.SetString(EntityId, Type, "FontPath", value); }

    /// <summary>Whether &lt;b&gt;, &lt;color=#RRGGBB&gt; and &lt;size=N&gt; tags are interpreted.</summary>
    public bool RichText { get => ComponentProperties.GetBool(EntityId, Type, "RichText"); set => ComponentProperties.SetBool(EntityId, Type, "RichText", value); }
    public bool Wrap { get => ComponentProperties.GetBool(EntityId, Type, "Wrap"); set => ComponentProperties.SetBool(EntityId, Type, "Wrap", value); }
    public UITextOverflow Overflow { get => (UITextOverflow)ComponentProperties.GetInt(EntityId, Type, "Overflow"); set => ComponentProperties.SetInt(EntityId, Type, "Overflow", (int)value); }
    public float LetterSpacing { get => ComponentProperties.GetFloat(EntityId, Type, "LetterSpacing"); set => ComponentProperties.SetFloat(EntityId, Type, "LetterSpacing", value); }
    public float LineSpacing { get => ComponentProperties.GetFloat(EntityId, Type, "LineSpacing", 1.0f); set => ComponentProperties.SetFloat(EntityId, Type, "LineSpacing", value); }
    public bool Bold { get => ComponentProperties.GetBool(EntityId, Type, "Bold"); set => ComponentProperties.SetBool(EntityId, Type, "Bold", value); }
    public bool Italic { get => ComponentProperties.GetBool(EntityId, Type, "Italic"); set => ComponentProperties.SetBool(EntityId, Type, "Italic", value); }
    public Vector4 OutlineColor { get => ComponentProperties.GetVector4(EntityId, Type, "OutlineColor"); set => ComponentProperties.SetVector4(EntityId, Type, "OutlineColor", value); }
    public float OutlineWidth { get => ComponentProperties.GetFloat(EntityId, Type, "OutlineWidth"); set => ComponentProperties.SetFloat(EntityId, Type, "OutlineWidth", value); }
    public Vector4 ShadowColor { get => ComponentProperties.GetVector4(EntityId, Type, "ShadowColor"); set => ComponentProperties.SetVector4(EntityId, Type, "ShadowColor", value); }
    public Vector2 ShadowOffset { get => ComponentProperties.GetVector2(EntityId, Type, "ShadowOffset"); set => ComponentProperties.SetVector2(EntityId, Type, "ShadowOffset", value); }
    public float ShadowSoftness { get => ComponentProperties.GetFloat(EntityId, Type, "ShadowSoftness"); set => ComponentProperties.SetFloat(EntityId, Type, "ShadowSoftness", value); }

    /// <summary>Inner padding: left, bottom, right, top.</summary>
    public Vector4 Padding { get => ComponentProperties.GetVector4(EntityId, Type, "Padding"); set => ComponentProperties.SetVector4(EntityId, Type, "Padding", value); }
}

public enum UITextOverflow { Overflow, Clip, Ellipsis }
