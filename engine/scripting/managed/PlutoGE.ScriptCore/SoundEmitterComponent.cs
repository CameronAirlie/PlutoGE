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

    public void Play() => ScriptBridge.SoundEmitterPlay(EntityId);
    public void Pause() => ScriptBridge.SoundEmitterPause(EntityId);
    public void Stop() => ScriptBridge.SoundEmitterStop(EntityId);
}
