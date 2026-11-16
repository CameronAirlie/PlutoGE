using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

/// <summary>The temporal upscaler (DLSS, FSR 2) a game's settings choose. See docs/DLSS.md and docs/FSR2.md.</summary>
public static partial class GraphicsSettings
{
    /// <summary>Whether this host lets scripts choose the upscaler.</summary>
    public static bool UpscalerSettingsSupported => ScriptBridge.UpscalerSupported;

    /// <summary>
    /// The upscaler frames render with: the project's (Project Settings > Runtime Upscaler) until a script chooses
    /// another with <see cref="TrySetUpscaler"/>. <see cref="UpscalerSettings.Off"/> when the host has no bridge.
    /// </summary>
    public static UpscalerSettings Upscaler
    {
        get
        {
            var request = new ScriptBridge.NativeUpscalerRequest();
            return ScriptBridge.ControlUpscaler(ScriptBridge.UpscalerOperation.Get, ref request)
                ? new UpscalerSettings((TemporalUpscaler)request.Technology, (UpscalerQuality)request.Quality, request.Sharpness)
                : UpscalerSettings.Off;
        }
    }

    /// <summary>Whether a script's choice (rather than the project's) is in force.</summary>
    public static bool IsUpscalerOverridden
    {
        get
        {
            var request = new ScriptBridge.NativeUpscalerRequest();
            return ScriptBridge.ControlUpscaler(ScriptBridge.UpscalerOperation.Get, ref request) && request.Overridden != 0;
        }
    }

    /// <summary>
    /// Whether this machine can run <paramref name="technology"/> (<see cref="TemporalUpscaler.None"/> always can).
    /// DLSS needs Vulkan, a supported NVIDIA GPU and driver; FSR 2 needs Vulkan. The reason reads well in a menu.
    /// </summary>
    public static UpscalerSupport GetUpscalerSupport(TemporalUpscaler technology)
    {
        if (!Enum.IsDefined(technology)) throw new ArgumentOutOfRangeException(nameof(technology));
        if (technology == TemporalUpscaler.None) return new UpscalerSupport(true, string.Empty);
        var request = new ScriptBridge.NativeUpscalerRequest { Technology = (int)technology };
        if (!ScriptBridge.ControlUpscaler(ScriptBridge.UpscalerOperation.GetSupport, ref request))
            return new UpscalerSupport(false, "This host does not support upscalers.");
        return new UpscalerSupport(request.Supported != 0, ScriptBridge.UpscalerRequestReason(ref request));
    }

    /// <summary>
    /// Renders with <paramref name="settings"/> until the game stops (or <see cref="TryResetUpscaler"/>). An upscaler
    /// this machine cannot run is accepted and falls back to native TAA, so check <see cref="GetUpscalerSupport"/> to
    /// offer only what works. False when the host has no bridge.
    /// </summary>
    public static bool TrySetUpscaler(UpscalerSettings settings)
    {
        if (!settings.IsValid) throw new ArgumentOutOfRangeException(nameof(settings));
        var request = new ScriptBridge.NativeUpscalerRequest
        {
            Technology = (int)settings.Technology, Quality = (int)settings.Quality, Sharpness = settings.Sharpness,
        };
        return ScriptBridge.ControlUpscaler(ScriptBridge.UpscalerOperation.Set, ref request);
    }

    /// <summary>Back to the project's upscaler.</summary>
    public static bool TryResetUpscaler()
    {
        var request = new ScriptBridge.NativeUpscalerRequest();
        return ScriptBridge.ControlUpscaler(ScriptBridge.UpscalerOperation.Reset, ref request);
    }
}
