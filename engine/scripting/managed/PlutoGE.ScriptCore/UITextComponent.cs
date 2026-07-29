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
}
