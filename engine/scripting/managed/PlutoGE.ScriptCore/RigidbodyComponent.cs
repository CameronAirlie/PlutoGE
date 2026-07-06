using System.Numerics;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

public sealed class RigidbodyComponent : ComponentReference
{
    internal RigidbodyComponent(uint entityId)
        : base(entityId)
    {
    }

    internal override ScriptBridge.NativeComponentType ComponentType => ScriptBridge.NativeComponentType.Rigidbody;

    public float Mass
    {
        get => ScriptBridge.GetRigidbodyMass(EntityId);
        set => ScriptBridge.SetRigidbodyMass(EntityId, value);
    }

    public float LinearDrag
    {
        get => ScriptBridge.GetRigidbodyLinearDrag(EntityId);
        set => ScriptBridge.SetRigidbodyLinearDrag(EntityId, value);
    }

    public float AngularDrag
    {
        get => ScriptBridge.GetRigidbodyAngularDrag(EntityId);
        set => ScriptBridge.SetRigidbodyAngularDrag(EntityId, value);
    }

    public float Friction
    {
        get => ScriptBridge.GetRigidbodyFriction(EntityId);
        set => ScriptBridge.SetRigidbodyFriction(EntityId, value);
    }

    public bool UseGravity
    {
        get => ScriptBridge.GetRigidbodyUseGravity(EntityId);
        set => ScriptBridge.SetRigidbodyUseGravity(EntityId, value);
    }

    public bool IsKinematic
    {
        get => ScriptBridge.GetRigidbodyKinematic(EntityId);
        set => ScriptBridge.SetRigidbodyKinematic(EntityId, value);
    }

    public bool FreezeRotation
    {
        get => ScriptBridge.GetRigidbodyFreezeRotation(EntityId);
        set => ScriptBridge.SetRigidbodyFreezeRotation(EntityId, value);
    }

    public Vector3 Velocity
    {
        get => ScriptBridge.GetRigidbodyVelocity(EntityId);
        set => ScriptBridge.SetRigidbodyVelocity(EntityId, value);
    }

    public Vector3 AngularVelocity
    {
        get => ScriptBridge.GetRigidbodyAngularVelocity(EntityId);
        set => ScriptBridge.SetRigidbodyAngularVelocity(EntityId, value);
    }

    public void AddForce(Vector3 force)
    {
        ScriptBridge.AddRigidbodyForce(EntityId, force);
    }

    public void AddImpulse(Vector3 impulse)
    {
        ScriptBridge.AddRigidbodyImpulse(EntityId, impulse);
    }

    public void AddForceAtPosition(Vector3 force, Vector3 worldPosition)
    {
        ScriptBridge.AddRigidbodyForceAtPosition(EntityId, force, worldPosition);
    }

    public void AddImpulseAtPosition(Vector3 impulse, Vector3 worldPosition)
    {
        ScriptBridge.AddRigidbodyImpulseAtPosition(EntityId, impulse, worldPosition);
    }
}
