using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

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
}
