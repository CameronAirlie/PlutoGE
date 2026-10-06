namespace PlutoGE.ScriptCore;

/// <summary>Live display controls. Scene-specific graphics must be reapplied after a scene load.</summary>
public static class GraphicsSettings
{
    public static bool QualitySupported => Native.ScriptBridge.GraphicsQualitySupported;

    public static bool TryGetPreset(GraphicsPreset preset, out GraphicsQuality quality)
    {
        if (!Enum.IsDefined(preset)) throw new ArgumentOutOfRangeException(nameof(preset));
        bool success = Native.ScriptBridge.TryGetGraphicsQuality((int)preset, out var value);
        quality = success ? GraphicsQuality.FromNative(value) : new GraphicsQuality { Enabled = false };
        return success;
    }

    public static bool TryGetQuality(out GraphicsQuality quality)
    {
        bool success = Native.ScriptBridge.TryGetGraphicsQuality(-1, out var value);
        quality = success ? GraphicsQuality.FromNative(value) : new GraphicsQuality { Enabled = false };
        return success;
    }

    public static bool TryApplyPreset(GraphicsPreset preset) =>
        TryGetPreset(preset, out var quality) && TryApplyQuality(quality);

    public static bool TryApplyQuality(GraphicsQuality quality)
    {
        ArgumentNullException.ThrowIfNull(quality);
        return Native.ScriptBridge.TrySetGraphicsQuality(quality.ToNative());
    }

    /// <summary>Restores authored scene quality without changing scene assets.</summary>
    public static bool TryResetQuality() => TryApplyQuality(new GraphicsQuality { Enabled = false });

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
