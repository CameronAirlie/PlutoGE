using System.Numerics;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

public enum ColliderShape
{
    Box = 0,
    Sphere = 1,
    Capsule = 2,
}

public sealed class ColliderComponent : ComponentReference
{
    internal ColliderComponent(uint entityId)
        : base(entityId)
    {
    }

    internal override ScriptBridge.NativeComponentType ComponentType => ScriptBridge.NativeComponentType.Collider;

    public ColliderShape Shape
    {
        get => (ColliderShape)ScriptBridge.GetColliderShape(EntityId);
        set => ScriptBridge.SetColliderShape(EntityId, (int)value);
    }

    public Vector3 Center
    {
        get => ScriptBridge.GetColliderCenter(EntityId);
        set => ScriptBridge.SetColliderCenter(EntityId, value);
    }

    public Vector3 Size
    {
        get => ScriptBridge.GetColliderSize(EntityId);
        set => ScriptBridge.SetColliderSize(EntityId, value);
    }

    public float Radius
    {
        get => ScriptBridge.GetColliderRadius(EntityId);
        set => ScriptBridge.SetColliderRadius(EntityId, value);
    }

    public float Height
    {
        get => ScriptBridge.GetColliderHeight(EntityId);
        set => ScriptBridge.SetColliderHeight(EntityId, value);
    }

    public bool IsTrigger
    {
        get => ScriptBridge.GetColliderTrigger(EntityId);
        set => ScriptBridge.SetColliderTrigger(EntityId, value);
    }

    public bool BlocksAudio
    {
        get => ScriptBridge.GetColliderBlocksAudio(EntityId);
        set => ScriptBridge.SetColliderBlocksAudio(EntityId, value);
    }
}
