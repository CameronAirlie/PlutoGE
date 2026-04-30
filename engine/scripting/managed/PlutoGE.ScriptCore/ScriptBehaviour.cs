using System.Numerics;

namespace PlutoGE.ScriptCore;

public abstract class ScriptBehaviour
{
    public uint EntityId { get; internal set; }

    protected Vector3 Rotation
    {
        get => Native.ScriptBridge.GetEntityRotation(EntityId);
        set => Native.ScriptBridge.SetEntityRotation(EntityId, value);
    }

    public virtual void OnCreate()
    {
    }

    public virtual void OnUpdate(float deltaTime)
    {
    }
}