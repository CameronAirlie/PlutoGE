using System.Numerics;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

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
}
