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

    private bool _enabled = true;
    private bool _started;

    /// <summary>
    /// A disabled script receives no OnUpdate, OnFixedUpdate, OnLateUpdate, collision or animation-event callbacks;
    /// its timers keep running. Changing it calls <see cref="OnEnable"/> or <see cref="OnDisable"/> once the script
    /// has started. Use it to switch a behaviour off (a dead enemy's AI) without removing it.
    /// </summary>
    public bool Enabled
    {
        get => _enabled;
        set
        {
            if (_enabled == value)
            {
                return;
            }

            _enabled = value;
            if (!_started || IsDestroyed)
            {
                return;
            }

            if (value)
            {
                OnEnable();
            }
            else
            {
                OnDisable();
            }
        }
    }

    // OnEnable follows OnCreate for a script that starts enabled.
    internal void MarkStarted()
    {
        _started = true;
        if (_enabled)
        {
            OnEnable();
        }
    }

    // OnDisable precedes OnDestroy for a script that is still enabled.
    internal void MarkStopping()
    {
        if (_started && _enabled && !IsDestroyed)
        {
            OnDisable();
        }

        _started = false;
    }

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

    /// <summary>Runs after OnCreate when the script starts enabled, and whenever <see cref="Enabled"/> becomes true.</summary>
    public virtual void OnEnable()
    {
    }

    /// <summary>Runs whenever <see cref="Enabled"/> becomes false, and before OnDestroy when the script is still enabled.</summary>
    public virtual void OnDisable()
    {
    }

    public virtual void OnCollisionEnter(GameObject other)
    {
    }

    /// <summary>
    /// The first frame two solid colliders touch, with where and how hard. The default implementation calls
    /// <see cref="OnCollisionEnter(GameObject)"/>, so override whichever form you need.
    /// </summary>
    public virtual void OnCollisionEnter(Collision collision)
    {
        OnCollisionEnter(collision.Other);
    }

    /// <summary>
    /// A moving body (dynamic or kinematic rigidbody) started overlapping a trigger collider. Both the trigger's
    /// scripts and the body's scripts receive it, each with the other entity.
    /// </summary>
    public virtual void OnTriggerEnter(GameObject other)
    {
    }

    /// <summary>The body and the trigger stopped overlapping (also when either is disabled or destroyed).</summary>
    public virtual void OnTriggerExit(GameObject other)
    {
    }

    public virtual void OnCollisionExit(GameObject other)
    {
    }

    public virtual void OnAnimationEvent(AnimationEvent animationEvent)
    {
    }
}
