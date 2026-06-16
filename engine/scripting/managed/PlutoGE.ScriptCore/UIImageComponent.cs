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
}
