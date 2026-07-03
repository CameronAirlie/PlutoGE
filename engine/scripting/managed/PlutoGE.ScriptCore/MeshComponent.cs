using PlutoGE.ScriptCore.Native;
using System.Numerics;

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

    public Vector3 Color
    {
        get => ScriptBridge.GetMeshColor(EntityId);
        set => ScriptBridge.SetMeshColor(EntityId, value);
    }

    public Vector3 Emission
    {
        get => ScriptBridge.GetMeshEmission(EntityId);
        set => ScriptBridge.SetMeshEmission(EntityId, value);
    }
}
