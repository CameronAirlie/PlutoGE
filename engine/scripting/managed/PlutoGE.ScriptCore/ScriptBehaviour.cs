using System.Numerics;

namespace PlutoGE.ScriptCore;

public abstract class ScriptBehaviour
{
    private uint _entityId;
    private GameObject? _gameObject;

    public uint EntityId
    {
        get => _entityId;
        internal set
        {
            if (_entityId == value)
            {
                return;
            }

            _entityId = value;
            _gameObject = value == 0 ? null : new GameObject(value);
        }
    }

    protected GameObject GameObject => _gameObject ??= new GameObject(EntityId);

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

    public virtual void OnLateUpdate(float deltaTime)
    {
    }

    public virtual void OnCollisionEnter(GameObject other)
    {
    }

    public virtual void OnCollisionExit(GameObject other)
    {
    }

    public virtual void OnAnimationEvent(AnimationEvent animationEvent)
    {
    }
}
