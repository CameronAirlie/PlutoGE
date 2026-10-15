using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

public sealed class SoundEmitterComponent : ComponentReference
{
    internal SoundEmitterComponent(uint entityId)
        : base(entityId)
    {
    }

    internal override ScriptBridge.NativeComponentType ComponentType => ScriptBridge.NativeComponentType.SoundEmitter;

    public bool Playing => ScriptBridge.GetSoundEmitterPlaying(EntityId);

    public string Clip
    {
        get => ScriptBridge.GetSoundEmitterClipReference(EntityId);
        set => ScriptBridge.SetSoundEmitterClipReference(EntityId, value);
    }

    public bool Looping
    {
        get => ScriptBridge.GetSoundEmitterLooping(EntityId);
        set => ScriptBridge.SetSoundEmitterLooping(EntityId, value);
    }

    public bool Spatialized
    {
        get => ScriptBridge.GetSoundEmitterSpatialized(EntityId);
        set => ScriptBridge.SetSoundEmitterSpatialized(EntityId, value);
    }

    public bool PlayOnAwake
    {
        get => ScriptBridge.GetSoundEmitterPlayOnAwake(EntityId);
        set => ScriptBridge.SetSoundEmitterPlayOnAwake(EntityId, value);
    }

    public float Volume
    {
        get => ScriptBridge.GetSoundEmitterVolume(EntityId);
        set => ScriptBridge.SetSoundEmitterVolume(EntityId, value);
    }

    public float Pitch
    {
        get => ScriptBridge.GetSoundEmitterPitch(EntityId);
        set => ScriptBridge.SetSoundEmitterPitch(EntityId, value);
    }

    /// <summary>Spatialized emitters play at full volume within this distance.</summary>
    public float MinDistance
    {
        get => ScriptBridge.GetSoundEmitterRange(EntityId, ScriptBridge.SoundEmitterRange.MinDistance);
        set => ScriptBridge.SetSoundEmitterRange(EntityId, ScriptBridge.SoundEmitterRange.MinDistance, value);
    }

    /// <summary>Spatialized emitters are silent beyond this distance.</summary>
    public float MaxDistance
    {
        get => ScriptBridge.GetSoundEmitterRange(EntityId, ScriptBridge.SoundEmitterRange.MaxDistance);
        set => ScriptBridge.SetSoundEmitterRange(EntityId, ScriptBridge.SoundEmitterRange.MaxDistance, value);
    }

    /// <summary>Falloff steepness between MinDistance and MaxDistance.</summary>
    public float Rolloff
    {
        get => ScriptBridge.GetSoundEmitterRange(EntityId, ScriptBridge.SoundEmitterRange.Rolloff);
        set => ScriptBridge.SetSoundEmitterRange(EntityId, ScriptBridge.SoundEmitterRange.Rolloff, value);
    }

    public void Play() => ScriptBridge.SoundEmitterPlay(EntityId);
    public void PlayOneShot() => ScriptBridge.SoundEmitterPlayOneShot(EntityId);
    public void PlayOneShot(float volumeScale, float pitchScale) => ScriptBridge.SoundEmitterPlayOneShot(EntityId, volumeScale, pitchScale);
    public void Pause() => ScriptBridge.SoundEmitterPause(EntityId);
    public void Stop() => ScriptBridge.SoundEmitterStop(EntityId);
}
