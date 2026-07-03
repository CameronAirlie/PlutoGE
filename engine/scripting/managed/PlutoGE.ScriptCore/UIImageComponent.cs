using System.Numerics;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

public sealed class UIImageComponent : ComponentReference
{
    internal UIImageComponent(uint entityId) : base(entityId) {}

    internal override ScriptBridge.NativeComponentType ComponentType => ScriptBridge.NativeComponentType.UIImage;

    public Vector3 Color
    {
        get => ScriptBridge.GetUIImageColor(EntityId);
        set => ScriptBridge.SetUIImageColor(EntityId, value);
    }

    public float Alpha
    {
        get => ScriptBridge.GetUIImageAlpha(EntityId);
        set => ScriptBridge.SetUIImageAlpha(EntityId, value);
    }

    public string Texture
    {
        get => ScriptBridge.GetUIImageTexture(EntityId);
        set => ScriptBridge.SetUIImageTexture(EntityId, value);
    }

    public bool PreserveAspect
    {
        get => ScriptBridge.GetUIImagePreserveAspect(EntityId);
        set => ScriptBridge.SetUIImagePreserveAspect(EntityId, value);
    }

    public float FillAmount
    {
        get => ScriptBridge.GetUIImageFillAmount(EntityId);
        set => ScriptBridge.SetUIImageFillAmount(EntityId, value);
    }
}
