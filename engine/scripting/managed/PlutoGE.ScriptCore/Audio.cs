using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

/// <summary>Preparation of scene-owned audio resources. Call during loading, not during frame updates.</summary>
public static class Audio
{
    /// <summary>Synchronously decodes and uploads an asset. Repeated calls reuse the engine clip cache.</summary>
    public static bool PreloadClip(string assetReference)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(assetReference);
        return ScriptBridge.PreloadAudioClip(assetReference);
    }

    /// <summary>Reserves total playback capacity, including active voices, up to the engine voice limit.</summary>
    public static void PrewarmVoices(int count)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(count);
        ScriptBridge.PrewarmAudioVoices(count);
    }
}
