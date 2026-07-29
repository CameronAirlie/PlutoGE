using System.Numerics;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

public enum CanvasScaleMode { ConstantPixels, ScaleWithScreenSize, ConstantPhysicalSize }

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
}
