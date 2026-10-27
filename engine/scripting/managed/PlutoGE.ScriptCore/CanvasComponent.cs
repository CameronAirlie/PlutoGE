using System.Numerics;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

public enum CanvasScaleMode { ConstantPixels, ScaleWithScreenSize, ConstantPhysicalSize }
public enum CanvasRenderMode { ScreenSpace, WorldScreenSpace, ScreenSpaceCamera, WorldSpace }
public enum CanvasWorldSizeMode { WorldUnits, ConstantScreenSize, DistanceScaled }
/// <summary>Native draws the entity-based UI under the canvas; RmlUi shows the canvas's RML document.</summary>
public enum UIRenderBackend { Native, RmlUi }

public sealed class CanvasComponent : ComponentReference
{
    internal CanvasComponent(uint entityId) : base(entityId) {}

    internal override ScriptBridge.NativeComponentType ComponentType => ScriptBridge.NativeComponentType.Canvas;

    public float ScaleFactor
    {
        get => ScriptBridge.GetCanvasScaleFactor(EntityId);
        set => ScriptBridge.SetCanvasScaleFactor(EntityId, value);
    }

    public int SortingOrder
    {
        get => ScriptBridge.GetCanvasSortingOrder(EntityId);
        set => ScriptBridge.SetCanvasSortingOrder(EntityId, value);
    }

    public CanvasScaleMode ScaleMode
    {
        get => (CanvasScaleMode)ScriptBridge.GetCanvasScaleMode(EntityId);
        set => ScriptBridge.SetCanvasScaleMode(EntityId, (int)value);
    }

    public Vector2 ReferenceResolution
    {
        get => ScriptBridge.GetCanvasReferenceResolution(EntityId);
        set => ScriptBridge.SetCanvasReferenceResolution(EntityId, value);
    }

    public UIRenderBackend Backend
    {
        get => (UIRenderBackend)ComponentProperties.GetInt(EntityId, Type, "Backend");
        set => ComponentProperties.SetInt(EntityId, Type, "Backend", (int)value);
    }

    /// <summary>Screen space fills the viewport; world screen space floats a billboard at the entity.</summary>
    public CanvasRenderMode RenderMode
    {
        get => (CanvasRenderMode)ComponentProperties.GetInt(EntityId, Type, "RenderMode");
        set => ComponentProperties.SetInt(EntityId, Type, "RenderMode", (int)value);
    }

    public CanvasWorldSizeMode WorldSizeMode
    {
        get => (CanvasWorldSizeMode)ComponentProperties.GetInt(EntityId, Type, "WorldSizeMode");
        set => ComponentProperties.SetInt(EntityId, Type, "WorldSizeMode", (int)value);
    }

    private const string Type = "CanvasComponent";

    /// <summary>0 matches the reference width, 1 its height (ScaleWithScreenSize).</summary>
    public float MatchWidthOrHeight
    {
        get => ComponentProperties.GetFloat(EntityId, Type, "MatchWidthOrHeight", 0.5f);
        set => ComponentProperties.SetFloat(EntityId, Type, "MatchWidthOrHeight", value);
    }
}
