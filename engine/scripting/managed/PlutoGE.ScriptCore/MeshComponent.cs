using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

public sealed class MeshComponent : ComponentReference
{
    internal MeshComponent(uint entityId)
        : base(entityId)
    {
    }

    internal override ScriptBridge.NativeComponentType ComponentType => ScriptBridge.NativeComponentType.Mesh;

    public bool Static
    {
        get => ScriptBridge.GetMeshStatic(EntityId);
        set => ScriptBridge.SetMeshStatic(EntityId, value);
    }
}
