namespace PlutoGE.ScriptCore;

/// <summary>A scheduled timer. Cancel it to stop it early; it stops by itself when its owner is destroyed.</summary>
public sealed class TimerHandle
{
    internal TimerHandle(Action action, double due, double interval, ScriptBehaviour? owner, bool unscaled, ulong generation)
    {
        Action = action;
        Due = due;
        Interval = interval;
        Owner = owner;
        Unscaled = unscaled;
        Generation = generation;
    }

    internal Action Action { get; }
    internal double Due { get; set; }
    internal double Interval { get; }
    internal ScriptBehaviour? Owner { get; }
    internal bool Unscaled { get; }
    internal ulong Generation { get; }

    /// <summary>False once the timer has fired (one-shot), was cancelled, or its owner was destroyed.</summary>
    public bool IsActive { get; internal set; } = true;

    public void Cancel() => IsActive = false;
}

/// <summary>
/// Runs code after a delay or on an interval without counting time in OnUpdate:
/// <code>
/// After(2.0f, () => door.Open());                // inside a ScriptBehaviour: stops if this script is destroyed
/// var blink = Every(0.5f, ToggleLight);
/// blink.Cancel();
/// Timers.After(1.0f, ShowHint, unscaled: true);  // keeps running while the game is paused (TimeScale 0)
/// </code>
/// Timers fire once per frame, before scripts update, in the order they become due. A timer owned by a script stops
/// when that script is destroyed; every timer stops when Play stops. An exception thrown by a timer is logged
/// and cancels that timer.
/// </summary>
public static class Timers
{
    private static readonly List<TimerHandle> Active = [];
    private static readonly List<TimerHandle> Due = [];
    private static ulong _lastTickFrame = ulong.MaxValue;

    /// <summary>Runs <paramref name="action"/> once after <paramref name="seconds"/>.</summary>
    public static TimerHandle After(float seconds, Action action, ScriptBehaviour? owner = null, bool unscaled = false) =>
        Schedule(seconds, 0.0, action, owner, unscaled);

    /// <summary>Runs <paramref name="action"/> every <paramref name="interval"/> seconds, first after one interval.</summary>
    public static TimerHandle Every(float interval, Action action, ScriptBehaviour? owner = null, bool unscaled = false)
    {
        if (interval <= 0.0f)
        {
            throw new ArgumentOutOfRangeException(nameof(interval), interval, "A repeating timer needs a positive interval.");
        }

        return Schedule(interval, interval, action, owner, unscaled);
    }

    // Play start and stop drop every timer.
    internal static void Clear()
    {
        foreach (var timer in Active)
            timer.Cancel();
        Active.Clear();
        Due.Clear();
    }

    /// <summary>Cancels every timer owned by <paramref name="owner"/>.</summary>
    public static void CancelAll(ScriptBehaviour owner)
    {
        foreach (var timer in Active)
        {
            if (ReferenceEquals(timer.Owner, owner))
            {
                timer.Cancel();
            }
        }
    }

    private static TimerHandle Schedule(float seconds, double interval, Action action, ScriptBehaviour? owner, bool unscaled)
    {
        ArgumentNullException.ThrowIfNull(action);
        var timer = new TimerHandle(action, Now(unscaled) + Math.Max(0.0f, seconds), interval, owner, unscaled,
            SceneManager.RuntimeGeneration);
        Active.Add(timer);
        return timer;
    }

    private static double Now(bool unscaled)
    {
        var clock = Native.ScriptBridge.GetRuntimeClock();
        return unscaled ? clock.UnscaledTime : clock.Time;
    }

    // Called by the bridge before the first script update of each frame; later calls in the same frame do nothing.
    internal static void Tick(ulong frame)
    {
        if (frame == _lastTickFrame || Active.Count == 0)
        {
            _lastTickFrame = frame;
            return;
        }

        _lastTickFrame = frame;
        var generation = SceneManager.RuntimeGeneration;
        var clock = Native.ScriptBridge.GetRuntimeClock();

        // Timers from an earlier Play session or a replaced scene, and timers of destroyed scripts, end silently.
        Active.RemoveAll(timer => !timer.IsActive || timer.Generation != generation || timer.Owner is { IsDestroyed: true });

        Due.Clear();
        foreach (var timer in Active)
        {
            if (timer.Due <= (timer.Unscaled ? clock.UnscaledTime : clock.Time))
            {
                Due.Add(timer);
            }
        }

        Due.Sort((left, right) => left.Due.CompareTo(right.Due));
        foreach (var timer in Due)
        {
            // An earlier timer this frame may have cancelled this one or destroyed its owner.
            if (!timer.IsActive || timer.Owner is { IsDestroyed: true })
            {
                continue;
            }

            if (timer.Interval > 0.0)
            {
                // Fire at most once per frame; a long frame does not replay missed ticks.
                var now = timer.Unscaled ? clock.UnscaledTime : clock.Time;
                timer.Due = Math.Max(timer.Due + timer.Interval, now);
            }
            else
            {
                timer.IsActive = false;
            }

            try
            {
                timer.Action();
            }
            catch (Exception exception)
            {
                timer.IsActive = false;
                var owner = timer.Owner is null ? "A timer" : $"A timer on {timer.Owner.GetType().Name}";
                Debug.LogError($"{owner} threw and was cancelled: {exception}");
            }
        }

        Active.RemoveAll(timer => !timer.IsActive);
    }
}
