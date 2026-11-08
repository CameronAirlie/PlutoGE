using System.Numerics;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

/// <summary>
/// Preparation of scene-owned audio resources (call during loading, not during frame updates) and the player's output
/// volume.
/// </summary>
/// <summary>A sound started with <see cref="Audio.PlayAt"/> or <see cref="Audio.Play2D"/>.</summary>
public readonly struct SoundHandle
{
    internal SoundHandle(ulong id) => Id = id;

    internal ulong Id { get; }

    /// <summary>False when nothing was started (Play was not running, or the clip reference was empty or missing).</summary>
    public bool IsValid => Id != 0;

    /// <summary>Stops the sound if it is still playing.</summary>
    public void Stop() => ScriptBridge.StopSceneSound(Id);
}

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

    /// <summary>
    /// Plays a clip once at a world position, with distance attenuation and occlusion like a sound emitter, without
    /// creating an entity. Use it for impacts, footsteps and pickups. Returns a handle to stop it early.
    /// </summary>
    /// <example><code>Audio.PlayAt(hitSound, collision.Point, volume: 0.8f, pitch: Random.Shared.NextSingle() * 0.2f + 0.9f);</code></example>
    public static SoundHandle PlayAt(string clipReference, Vector3 position, float volume = 1.0f, float pitch = 1.0f,
                                     float minDistance = 1.0f, float maxDistance = 25.0f) =>
        new(ScriptBridge.PlaySceneSound(clipReference, position, volume, pitch, spatial: true, minDistance, maxDistance));

    /// <summary>Plays a clip once at full volume regardless of where the listener is (interface sounds, stingers).</summary>
    public static SoundHandle Play2D(string clipReference, float volume = 1.0f, float pitch = 1.0f) =>
        new(ScriptBridge.PlaySceneSound(clipReference, Vector3.Zero, volume, pitch, spatial: false, 1.0f, 25.0f));

    /// <summary>Reserves total playback capacity, including active voices, up to the engine voice limit.</summary>
    public static void PrewarmVoices(int count)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(count);
        ScriptBridge.PrewarmAudioVoices(count);
    }
}
