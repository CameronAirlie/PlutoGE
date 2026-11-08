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

    /// <summary>Set once OnDestroy has run or the engine released this script; timers it owns stop.</summary>
    internal bool IsDestroyed { get; set; }

    /// <summary>Runs <paramref name="action"/> once after <paramref name="seconds"/> of game time (see <see cref="Timers.After"/>).</summary>
    protected TimerHandle After(float seconds, Action action, bool unscaled = false) =>
        Timers.After(seconds, action, this, unscaled);

    /// <summary>Runs <paramref name="action"/> every <paramref name="interval"/> seconds (see <see cref="Timers.Every"/>).</summary>
    protected TimerHandle Every(float interval, Action action, bool unscaled = false) =>
        Timers.Every(interval, action, this, unscaled);

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

    public virtual void OnFixedUpdate(float fixedDeltaTime)
    {
    }

    public virtual void OnLateUpdate(float deltaTime)
    {
    }

    public virtual void OnDestroy()
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
