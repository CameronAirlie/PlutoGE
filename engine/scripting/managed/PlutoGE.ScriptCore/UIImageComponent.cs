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

    // Through the generic property bridge.

    private const string Type = "UIImageComponent";

    /// <summary>Colour with alpha.</summary>
    public Vector4 Rgba { get => ComponentProperties.GetVector4(EntityId, Type, "Color"); set => ComponentProperties.SetVector4(EntityId, Type, "Color", value); }
    public float CornerRadius { get => ComponentProperties.GetFloat(EntityId, Type, "CornerRadius"); set => ComponentProperties.SetFloat(EntityId, Type, "CornerRadius", value); }

    /// <summary>Where a radial fill or procedural arc starts, in degrees.</summary>
    public float StartAngle { get => ComponentProperties.GetFloat(EntityId, Type, "StartAngle"); set => ComponentProperties.SetFloat(EntityId, Type, "StartAngle", value); }
    public bool FillClockwise { get => ComponentProperties.GetBool(EntityId, Type, "FillClockwise"); set => ComponentProperties.SetBool(EntityId, Type, "FillClockwise", value); }
}
