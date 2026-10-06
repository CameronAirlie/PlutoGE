namespace PlutoGE.ScriptCore;

public enum GraphicsPreset { Low, Medium, High, Ultra }

/// <summary>Runtime quality ceilings, independent of scene assets and display settings.
/// Start with TryGetPreset, customise with a record 'with' expression, then TryApplyQuality.</summary>
public sealed record GraphicsQuality
{
    public bool Enabled { get; init; } = true;
    public int ShadowResolution { get; init; } = 2048;
    public int ShadowCascades { get; init; } = 4;
    public float ShadowDistance { get; init; } = 150;
    public bool CascadedShadows { get; init; }
    public bool AmbientOcclusion { get; init; } = true;
    public bool GlobalIllumination { get; init; } = true;
    public bool Reflections { get; init; } = true;
    public bool Volumetrics { get; init; } = true;
    public bool Bloom { get; init; } = true;
    public bool DepthOfField { get; init; } = true;
    public bool MotionBlur { get; init; } = true;

    internal Native.GraphicsQualityNative ToNative()
    {
        if (ShadowResolution is < 256 or > 8192) throw new ArgumentOutOfRangeException(nameof(ShadowResolution));
        if (ShadowCascades is < 1 or > 4) throw new ArgumentOutOfRangeException(nameof(ShadowCascades));
        if (!float.IsFinite(ShadowDistance) || ShadowDistance is < 1 or > 10000)
            throw new ArgumentOutOfRangeException(nameof(ShadowDistance));
        return new()
        {
            ShadowResolution = (uint)ShadowResolution, ShadowCascades = (uint)ShadowCascades,
            ShadowDistance = ShadowDistance,
            Flags = (Enabled ? 1u : 0) | (CascadedShadows ? 2u : 0) | (AmbientOcclusion ? 4u : 0) |
                (GlobalIllumination ? 8u : 0) | (Reflections ? 16u : 0) | (Volumetrics ? 32u : 0) |
                (Bloom ? 64u : 0) | (DepthOfField ? 128u : 0) | (MotionBlur ? 256u : 0)
        };
    }

    internal static GraphicsQuality FromNative(Native.GraphicsQualityNative value) => new()
    {
        ShadowResolution = (int)value.ShadowResolution, ShadowCascades = (int)value.ShadowCascades,
        ShadowDistance = value.ShadowDistance, Enabled = (value.Flags & 1) != 0,
        CascadedShadows = (value.Flags & 2) != 0, AmbientOcclusion = (value.Flags & 4) != 0,
        GlobalIllumination = (value.Flags & 8) != 0, Reflections = (value.Flags & 16) != 0,
        Volumetrics = (value.Flags & 32) != 0, Bloom = (value.Flags & 64) != 0,
        DepthOfField = (value.Flags & 128) != 0, MotionBlur = (value.Flags & 256) != 0
    };
}
