using System.Numerics;

namespace PlutoGE.ScriptCore;

/// <summary>The acoustic character an <see cref="AudioEnvironmentVolumeComponent"/> starts from.</summary>
public enum AudioEnvironmentPreset
{
    Custom = 0,
    Cave = 1,
    Forest = 2,
    City = 3,
    Field = 4,
    Room = 5,
    Hall = 6,
    Underwater = 7,
}

public enum AudioEnvironmentShape
{
    Box = 0,
    Sphere = 1,
}

/// <summary>
/// A space with its own acoustics: reverb, echo and muffling for the listener inside it (a cave, a hall, a closed hut).
/// Volumes blend over <see cref="BlendDistance"/> and the highest <see cref="Priority"/> wins where they overlap. A
/// script can move one with the player's shelter (indoors and outdoors) or switch it off; the volume's box or sphere
/// follows its entity's transform.
/// </summary>
[NativeComponent("AudioEnvironmentVolumeComponent")]
public sealed class AudioEnvironmentVolumeComponent : RegisteredComponent
{
    internal AudioEnvironmentVolumeComponent(uint entityId) : base(entityId)
    {
    }

    public AudioEnvironmentPreset Preset { get => GetEnum<AudioEnvironmentPreset>("Preset"); set => SetEnum("Preset", value); }
    public AudioEnvironmentShape Shape { get => GetEnum<AudioEnvironmentShape>("Shape"); set => SetEnum("Shape", value); }
    /// <summary>The box's size in metres (Box shape).</summary>
    public Vector3 Size { get => GetVector3("Size"); set => SetVector3("Size", value); }
    /// <summary>The sphere's radius in metres (Sphere shape).</summary>
    public float Radius { get => GetFloat("Radius"); set => SetFloat("Radius", value); }
    /// <summary>Metres over which the volume fades in at its edge.</summary>
    public float BlendDistance { get => GetFloat("Blend Distance"); set => SetFloat("Blend Distance", value); }
    public int Priority { get => GetInt("Priority"); set => SetInt("Priority", value); }
    public float ReverbWet { get => GetFloat("Reverb Wet"); set => SetFloat("Reverb Wet", value); }
    public float ReverbDecay { get => GetFloat("Reverb Decay"); set => SetFloat("Reverb Decay", value); }
    public float ReverbDensity { get => GetFloat("Reverb Density"); set => SetFloat("Reverb Density", value); }
    public float ReverbDiffusion { get => GetFloat("Reverb Diffusion"); set => SetFloat("Reverb Diffusion", value); }
    public float EchoWet { get => GetFloat("Echo Wet"); set => SetFloat("Echo Wet", value); }
    public float EchoDelay { get => GetFloat("Echo Delay"); set => SetFloat("Echo Delay", value); }
    public float EchoFeedback { get => GetFloat("Echo Feedback"); set => SetFloat("Echo Feedback", value); }
    /// <summary>How much the space muffles what the listener hears, 0 not at all to 1 heavily (a low-pass strength).</summary>
    public float LowPass { get => GetFloat("Low Pass"); set => SetFloat("Low Pass", value); }
    public float Gain { get => GetFloat("Gain"); set => SetFloat("Gain", value); }
}
