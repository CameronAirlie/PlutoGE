using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

public sealed class AnimationComponent : ComponentReference
{
    internal AnimationComponent(uint entityId)
        : base(entityId)
    {
    }

    internal override ScriptBridge.NativeComponentType ComponentType => ScriptBridge.NativeComponentType.Animation;

    public int ClipCount => ScriptBridge.GetAnimationClipCount(EntityId);

    public int ClipIndex
    {
        get => ScriptBridge.GetAnimationClipIndex(EntityId);
        set => ScriptBridge.SetAnimationClipIndex(EntityId, value);
    }

    public bool Playing
    {
        get => ScriptBridge.GetAnimationPlaying(EntityId);
        set => ScriptBridge.SetAnimationPlaying(EntityId, value);
    }

    public bool Looping
    {
        get => ScriptBridge.GetAnimationLooping(EntityId);
        set => ScriptBridge.SetAnimationLooping(EntityId, value);
    }

    public bool Autoplay
    {
        get => ScriptBridge.GetAnimationAutoplay(EntityId);
        set => ScriptBridge.SetAnimationAutoplay(EntityId, value);
    }

    public float Speed
    {
        get => ScriptBridge.GetAnimationSpeed(EntityId);
        set => ScriptBridge.SetAnimationSpeed(EntityId, value);
    }

    public float Time
    {
        get => ScriptBridge.GetAnimationTime(EntityId);
        set => ScriptBridge.SetAnimationTime(EntityId, value);
    }

    public string GetClipName(int clipIndex)
    {
        return ScriptBridge.GetAnimationClipName(EntityId, clipIndex);
    }

    public float GetClipDuration(int clipIndex)
    {
        return ScriptBridge.GetAnimationClipDuration(EntityId, clipIndex);
    }

    public void Play()
    {
        ScriptBridge.AnimationPlay(EntityId);
    }

    public bool Play(string clipName)
    {
        for (var clipIndex = 0; clipIndex < ClipCount; clipIndex++)
        {
            if (!string.Equals(GetClipName(clipIndex), clipName, StringComparison.Ordinal))
            {
                continue;
            }

            ClipIndex = clipIndex;
            Play();
            return true;
        }

        return false;
    }

    public void Pause()
    {
        ScriptBridge.AnimationPause(EntityId);
    }

    public void Stop()
    {
        ScriptBridge.AnimationStop(EntityId);
    }

    public void PlayState(string stateName)
    {
        ScriptBridge.AnimationPlayState(EntityId, stateName);
    }

    public void SetBool(string parameterName, bool value)
    {
        ScriptBridge.SetAnimationBoolParameter(EntityId, parameterName, value);
    }

    public void SetFloat(string parameterName, float value)
    {
        ScriptBridge.SetAnimationFloatParameter(EntityId, parameterName, value);
    }

    public void SetInteger(string parameterName, int value)
    {
        ScriptBridge.SetAnimationIntParameter(EntityId, parameterName, value);
    }

    public void SetTrigger(string parameterName)
    {
        ScriptBridge.SetAnimationTriggerParameter(EntityId, parameterName);
    }

    public void ResetTrigger(string parameterName)
    {
        ScriptBridge.ResetAnimationTriggerParameter(EntityId, parameterName);
    }
}
