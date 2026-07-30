using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

/// <summary>Shared pause state for gameplay controllers and pause-menu scripts.</summary>
public static class GamePause
{
    public static bool IsPaused { get; internal set; }

    public static float TimeScale
    {
        get => ScriptBridge.GetSceneTimeScale();
        set => ScriptBridge.SetSceneTimeScale(MathF.Max(0.0f, value));
    }
}
