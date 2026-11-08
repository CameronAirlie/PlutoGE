using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

/// <summary>
/// Game time since Play started. Values advance once per frame before scripts update, and stand still while the
/// editor is paused. Scaled values follow <see cref="TimeScale"/>; unscaled values keep running at time scale 0,
/// which is what pause menus and UI animation need.
/// </summary>
public static class Time
{
    /// <summary>Seconds since the previous frame, scaled by <see cref="TimeScale"/> (the value passed to OnUpdate).</summary>
    public static float DeltaTime => ScriptBridge.GetRuntimeClock().DeltaTime;

    /// <summary>Real seconds since the previous frame, unaffected by <see cref="TimeScale"/>.</summary>
    public static float UnscaledDeltaTime => ScriptBridge.GetRuntimeClock().UnscaledDeltaTime;

    /// <summary>Seconds between physics steps (the value passed to OnFixedUpdate).</summary>
    public static float FixedDeltaTime => ScriptBridge.GetRuntimeClock().FixedDeltaTime;

    /// <summary>Scaled seconds since Play started.</summary>
    public static double TimeSinceStart => ScriptBridge.GetRuntimeClock().Time;

    /// <summary>Real seconds since Play started.</summary>
    public static double UnscaledTimeSinceStart => ScriptBridge.GetRuntimeClock().UnscaledTime;

    /// <summary>Frames since Play started; the first update is frame 1.</summary>
    public static ulong FrameCount => ScriptBridge.GetRuntimeClock().FrameCount;

    /// <summary>How fast game time runs: 1 is normal, 0.5 half speed, 0 stopped. Same as <see cref="GamePause.TimeScale"/>.</summary>
    public static float TimeScale
    {
        get => GamePause.TimeScale;
        set => GamePause.TimeScale = value;
    }
}
