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
        get => this is RegisteredComponent registered
            ? registered.GetProperty("Enabled") is "true" or "1"
            : ScriptBridge.GetComponentEnabled(EntityId, ComponentType);
        set
        {
            if (this is RegisteredComponent registered)
                registered.SetProperty("Enabled", value ? "true" : "false");
            else
                ScriptBridge.SetComponentEnabled(EntityId, ComponentType, value);
        }
    }
}
