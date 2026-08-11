using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

public sealed class ActiveRagdollComponent : ComponentReference
{
    internal ActiveRagdollComponent(uint entityId) : base(entityId) {}
    internal override ScriptBridge.NativeComponentType ComponentType => ScriptBridge.NativeComponentType.ActiveRagdoll;

    public float PositionStrength
    {
        get => ScriptBridge.GetActiveRagdollPositionStrength(EntityId);
        set => ScriptBridge.SetActiveRagdollPositionStrength(EntityId, Math.Max(0.0f, value));
    }

    public float RotationStrength
    {
        get => ScriptBridge.GetActiveRagdollRotationStrength(EntityId);
        set => ScriptBridge.SetActiveRagdollRotationStrength(EntityId, Math.Max(0.0f, value));
    }

    public float Damping
    {
        get => ScriptBridge.GetActiveRagdollDamping(EntityId);
        set => ScriptBridge.SetActiveRagdollDamping(EntityId, Math.Max(0.0f, value));
    }
}
