using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

public abstract class ComponentReference
{
    protected ComponentReference(uint entityId)
    {
        EntityId = entityId;
    }

    public uint EntityId { get; }
    public bool IsValid => EntityId != 0;
    public GameObject GameObject => new(EntityId);

    internal abstract ScriptBridge.NativeComponentType ComponentType { get; }

    public bool Enabled
    {
        get => ScriptBridge.GetComponentEnabled(EntityId, ComponentType);
        set => ScriptBridge.SetComponentEnabled(EntityId, ComponentType, value);
    }
}
