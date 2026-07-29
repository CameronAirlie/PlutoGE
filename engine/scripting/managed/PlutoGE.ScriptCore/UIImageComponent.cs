using System.Numerics;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

public enum UIImageType
{
    Simple, Sliced, FilledHorizontal, FilledVertical, FilledRadial,
    ProceduralCrosshair, ProceduralCircle, ProceduralArc, ProceduralRoundedRect
}

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

    public UIImageType ImageType
    {
        get => (UIImageType)ScriptBridge.GetUIImageType(EntityId);
        set => ScriptBridge.SetUIImageType(EntityId, (int)value);
    }

    public float Thickness
    {
        get => ScriptBridge.GetUIImageThickness(EntityId);
        set => ScriptBridge.SetUIImageThickness(EntityId, value);
    }
}
