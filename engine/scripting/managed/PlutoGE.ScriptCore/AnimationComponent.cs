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

    public bool RagdollEnabled
    {
        get => ScriptBridge.GetRagdollEnabled(EntityId);
        set => ScriptBridge.SetRagdollEnabled(EntityId, value);
    }

    public float RagdollWeight
    {
        get => ScriptBridge.GetRagdollWeight(EntityId);
        set => ScriptBridge.SetRagdollWeight(EntityId, Math.Clamp(value, 0.0f, 1.0f));
    }

    public void AddRagdollImpulse(System.Numerics.Vector3 impulse) => ScriptBridge.AddRagdollImpulse(EntityId, impulse);

    public void ResetRagdoll() => ScriptBridge.ResetRagdoll(EntityId);

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
    /// <summary>Sets a persistent, named two-bone constraint. Target and elbow pole are world
    /// positions sampled into mesh space at this call. Call each update for moving targets.
    /// The three named bones must form a direct parent chain. Constraints solve in insertion order.</summary>
    public bool SetTwoBoneIK(string id, GameObject mesh, string rootBone, string middleBone, string tipBone,
                             System.Numerics.Vector3 worldTarget, System.Numerics.Vector3 worldPole,
                             float weight = 1, System.Numerics.Quaternion? worldRotation = null, float rotationWeight = 0)
    {
        ArgumentNullException.ThrowIfNull(mesh);
        var request = new ScriptBridge.NativeSkeletalRequest {
            Operation = 1, Owner = EntityId, Mesh = mesh.EntityId,
            Position = ScriptBridge.NativeVector3.FromManaged(worldTarget),
            Pole = ScriptBridge.NativeVector3.FromManaged(worldPole),
            Rotation = ScriptBridge.NativeQuaternion.FromManaged(worldRotation ?? System.Numerics.Quaternion.Identity),
            Weight = weight, RotationWeight = worldRotation.HasValue ? rotationWeight : 0
        };
        return ScriptBridge.ControlSkeleton(ref request, id, rootBone, middleBone, tipBone);
    }
    public void ClearTwoBoneIK(string id)
    {
        var request = new ScriptBridge.NativeSkeletalRequest { Operation = 4, Owner = EntityId };
        ScriptBridge.ControlSkeleton(ref request, id);
    }
    public void ClearAllTwoBoneIK()
    {
        var request = new ScriptBridge.NativeSkeletalRequest { Operation = 5, Owner = EntityId };
        ScriptBridge.ControlSkeleton(ref request);
    }
}
