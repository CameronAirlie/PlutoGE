namespace PlutoGE.ScriptCore;

/// <summary>Live display controls. Scene-specific graphics must be reapplied after a scene load.</summary>
public static class GraphicsSettings
{
    public static bool IsSupported => Native.ScriptBridge.DisplaySettingsSupported;
    public static bool VSyncEnabled => Native.ScriptBridge.GetDisplayVSync();
    public static bool TrySetVSync(bool enabled) => Native.ScriptBridge.SetDisplayVSync(enabled);
    /// <summary>Sets the base directional shadow-map resolution for active lights in the current scene.</summary>
    public static bool TrySetDirectionalShadowResolution(int resolution)
    {
        if (resolution is < 256 or > 8192) throw new ArgumentOutOfRangeException(nameof(resolution));
        return Native.ScriptBridge.SetSceneShadowResolution(resolution);
    }
}
