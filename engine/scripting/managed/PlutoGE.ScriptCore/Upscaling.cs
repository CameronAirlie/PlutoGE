namespace PlutoGE.ScriptCore;

/// <summary>A temporal upscaler: renders fewer pixels and reconstructs the output resolution from frame history.
/// Values match the engine's render::rhi::TemporalUpscaler.</summary>
public enum TemporalUpscaler
{
    /// <summary>No upscaler: native resolution with the built-in TAA.</summary>
    None = 0,
    /// <summary>NVIDIA DLSS Super Resolution (Vulkan, supported NVIDIA GPUs).</summary>
    Dlss = 1,
    /// <summary>AMD FidelityFX Super Resolution 2 (Vulkan).</summary>
    Fsr2 = 2,
}

/// <summary>How far below the output resolution an upscaler renders. Values match render::rhi::UpscalerQuality.</summary>
public enum UpscalerQuality
{
    /// <summary>Half the output resolution per axis.</summary>
    Performance = 0,
    /// <summary>About 59% of the output resolution per axis.</summary>
    Balanced = 1,
    /// <summary>About 67% of the output resolution per axis.</summary>
    Quality = 2,
    /// <summary>A third of the output resolution per axis.</summary>
    UltraPerformance = 3,
    /// <summary>Full resolution: the upscaler only anti-aliases (DLAA with DLSS, native AA with FSR 2).</summary>
    NativeAntiAliasing = 4,
}

/// <summary>
/// The upscaler a game's settings choose: technology, quality mode and the sharpening applied after reconstruction
/// (0-1). Hand it to <see cref="GraphicsSettings.TrySetUpscaler"/>.
/// </summary>
public readonly record struct UpscalerSettings(TemporalUpscaler Technology, UpscalerQuality Quality = UpscalerQuality.Quality,
                                               float Sharpness = UpscalerSettings.DefaultSharpness)
{
    public const float DefaultSharpness = 0.25f;

    /// <summary>Native resolution with the built-in TAA.</summary>
    public static UpscalerSettings Off => new(TemporalUpscaler.None);

    /// <summary>Whether every value is in range (a defined technology and quality, a finite sharpness in 0-1).</summary>
    public bool IsValid => Enum.IsDefined(Technology) && Enum.IsDefined(Quality) &&
                           float.IsFinite(Sharpness) && Sharpness is >= 0.0f and <= 1.0f;
}

/// <summary>Whether this machine can run an upscaler, and why not when it cannot (for a settings screen).</summary>
public readonly record struct UpscalerSupport(bool Supported, string Reason);
