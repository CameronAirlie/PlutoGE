using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

/// <summary>
/// Preparation of scene-owned audio resources (call during loading, not during frame updates) and the player's output
/// volume.
/// </summary>
public static class Audio
{
    /// <summary>False in hosts without the audio output bridge; <see cref="OutputVolume"/> then reads 1 and ignores writes.</summary>
    public static bool OutputVolumeSupported => ScriptBridge.AudioOutputSupported;

    /// <summary>
    /// Master volume (0-1) applied after every listener and emitter: a player preference. It belongs to the application,
    /// not a scene, so it persists across scene loads; the engine does not save it.
    /// </summary>
    public static float OutputVolume
    {
        get => ScriptBridge.GetAudioOutputVolume();
        set => ScriptBridge.SetAudioOutputVolume(float.IsFinite(value) ? Math.Clamp(value, 0.0f, 1.0f) : 1.0f);
    }

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
