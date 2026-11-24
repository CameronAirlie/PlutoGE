namespace PlutoGE.ScriptCore;

public enum GraphicsPreset { Low, Medium, High, Ultra }

/// <summary>Runtime quality ceilings, independent of scene assets and display settings.
/// Start with TryGetPreset, customise with a record 'with' expression, then TryApplyQuality.</summary>
public sealed record GraphicsQuality
{
    // Accepted ranges; keep in sync with render::GraphicsQuality.
    public const int MinShadowResolution = 256, MaxShadowResolution = 8192;
    public const int MinShadowCascades = 1, MaxShadowCascades = 4;
    public const float MinShadowDistance = 1, MaxShadowDistance = 10000;
    public const float MinFoliageDetail = 0.25f, MaxFoliageDetail = 2;
    public const float MinGrassDensity = 0.1f, MinGrassDistance = 0.25f;

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
    /// <summary>Screen-space contact shadows from the sun and local lights.</summary>
    public bool ContactShadows { get; init; } = true;
    /// <summary>Traced shadows: local lights without a shadow map stop at walls, floors and terrain.</summary>
    public bool TracedShadows { get; init; } = true;
    /// <summary>Swaying foliage sways in the sun's shadows too. Off, those shadows hold the rest pose and stay cached
    /// (much cheaper under a dense canopy) while the plants still sway.</summary>
    public bool FoliageShadowSway { get; init; } = true;
    /// <summary>Instanced foliage detail ceiling: below 1, plants take coarser LODs and impostors from nearer.</summary>
    public float FoliageDetail { get; init; } = 1;
    /// <summary>Share of each detail grass layer's authored density.</summary>
    public float GrassDensity { get; init; } = 1;
    /// <summary>Share of each detail grass layer's authored draw and LOD distances.</summary>
    public float GrassDistance { get; init; } = 1;

    static void Require(bool valid, string name)
    {
        if (!valid) throw new ArgumentOutOfRangeException(name);
    }
    static bool Within(float value, float min, float max) => float.IsFinite(value) && value >= min && value <= max;

    internal Native.GraphicsQualityNative ToNative()
    {
        Require(ShadowResolution is >= MinShadowResolution and <= MaxShadowResolution, nameof(ShadowResolution));
        Require(ShadowCascades is >= MinShadowCascades and <= MaxShadowCascades, nameof(ShadowCascades));
        Require(Within(ShadowDistance, MinShadowDistance, MaxShadowDistance), nameof(ShadowDistance));
        Require(Within(FoliageDetail, MinFoliageDetail, MaxFoliageDetail), nameof(FoliageDetail));
        Require(Within(GrassDensity, MinGrassDensity, 1), nameof(GrassDensity));
        Require(Within(GrassDistance, MinGrassDistance, 1), nameof(GrassDistance));
        return new()
        {
            ShadowResolution = (uint)ShadowResolution, ShadowCascades = (uint)ShadowCascades,
            ShadowDistance = ShadowDistance,
            Flags = (Enabled ? 1u : 0) | (CascadedShadows ? 2u : 0) | (AmbientOcclusion ? 4u : 0) |
                (GlobalIllumination ? 8u : 0) | (Reflections ? 16u : 0) | (Volumetrics ? 32u : 0) |
                (Bloom ? 64u : 0) | (DepthOfField ? 128u : 0) | (MotionBlur ? 256u : 0) | (ContactShadows ? 512u : 0) |
                (TracedShadows ? 1024u : 0) | (FoliageShadowSway ? 2048u : 0),
            FoliageDetail = FoliageDetail, GrassDensity = GrassDensity, GrassDistance = GrassDistance
        };
    }

    internal static GraphicsQuality FromNative(Native.GraphicsQualityNative value) => new()
    {
        ShadowResolution = (int)value.ShadowResolution, ShadowCascades = (int)value.ShadowCascades,
        ShadowDistance = value.ShadowDistance, Enabled = (value.Flags & 1) != 0,
        CascadedShadows = (value.Flags & 2) != 0, AmbientOcclusion = (value.Flags & 4) != 0,
        GlobalIllumination = (value.Flags & 8) != 0, Reflections = (value.Flags & 16) != 0,
        Volumetrics = (value.Flags & 32) != 0, Bloom = (value.Flags & 64) != 0,
        DepthOfField = (value.Flags & 128) != 0, MotionBlur = (value.Flags & 256) != 0,
        ContactShadows = (value.Flags & 512) != 0, TracedShadows = (value.Flags & 1024) != 0,
        FoliageShadowSway = (value.Flags & 2048) != 0,
        FoliageDetail = value.FoliageDetail,
        GrassDensity = value.GrassDensity, GrassDistance = value.GrassDistance
    };
}
