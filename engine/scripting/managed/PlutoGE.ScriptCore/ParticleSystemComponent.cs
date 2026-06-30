using PlutoGE.ScriptCore.Native;
using System.Numerics;

namespace PlutoGE.ScriptCore;

public enum ParticleSimulationSpace
{
    Local = 0,
    World = 1,
}

public enum ParticleShape
{
    Point = 0,
    Sphere = 1,
    Box = 2,
    Cone = 3,
}

public sealed class ParticleSystemComponent : ComponentReference
{
    internal ParticleSystemComponent(uint entityId)
        : base(entityId)
    {
    }

    internal override ScriptBridge.NativeComponentType ComponentType => ScriptBridge.NativeComponentType.ParticleSystem;

    public bool Playing => ScriptBridge.GetParticleSystemPlaying(EntityId);
    public int ParticleCount => ScriptBridge.GetParticleSystemParticleCount(EntityId);

    public string ParticleSystemAsset
    {
        get => ScriptBridge.GetParticleSystemAssetReference(EntityId);
        set => ScriptBridge.SetParticleSystemAssetReference(EntityId, value);
    }

    public bool Looping
    {
        get => ScriptBridge.GetParticleSystemLooping(EntityId);
        set => ScriptBridge.SetParticleSystemLooping(EntityId, value);
    }

    public bool PlayOnAwake
    {
        get => ScriptBridge.GetParticleSystemPlayOnAwake(EntityId);
        set => ScriptBridge.SetParticleSystemPlayOnAwake(EntityId, value);
    }

    public float Duration
    {
        get => ScriptBridge.GetParticleSystemDuration(EntityId);
        set => ScriptBridge.SetParticleSystemDuration(EntityId, value);
    }

    public float StartLifetime
    {
        get => ScriptBridge.GetParticleSystemStartLifetime(EntityId);
        set => ScriptBridge.SetParticleSystemStartLifetime(EntityId, value);
    }

    public float StartSpeed
    {
        get => ScriptBridge.GetParticleSystemStartSpeed(EntityId);
        set => ScriptBridge.SetParticleSystemStartSpeed(EntityId, value);
    }

    public float StartSize
    {
        get => ScriptBridge.GetParticleSystemStartSize(EntityId);
        set => ScriptBridge.SetParticleSystemStartSize(EntityId, value);
    }

    public float GravityModifier
    {
        get => ScriptBridge.GetParticleSystemGravityModifier(EntityId);
        set => ScriptBridge.SetParticleSystemGravityModifier(EntityId, value);
    }

    public float EmissionRateOverTime
    {
        get => ScriptBridge.GetParticleSystemEmissionRate(EntityId);
        set => ScriptBridge.SetParticleSystemEmissionRate(EntityId, value);
    }

    public Vector3 StartColor
    {
        get => ScriptBridge.GetParticleSystemStartColor(EntityId);
        set => ScriptBridge.SetParticleSystemStartColor(EntityId, value);
    }

    public Vector3 ShapeSize
    {
        get => ScriptBridge.GetParticleSystemShapeSize(EntityId);
        set => ScriptBridge.SetParticleSystemShapeSize(EntityId, value);
    }

    public ParticleSimulationSpace SimulationSpace
    {
        get => (ParticleSimulationSpace)ScriptBridge.GetParticleSystemSimulationSpace(EntityId);
        set => ScriptBridge.SetParticleSystemSimulationSpace(EntityId, (int)value);
    }

    public ParticleShape Shape
    {
        get => (ParticleShape)ScriptBridge.GetParticleSystemShape(EntityId);
        set => ScriptBridge.SetParticleSystemShape(EntityId, (int)value);
    }

    public void Play() => ScriptBridge.ParticleSystemPlay(EntityId);
    public void Pause() => ScriptBridge.ParticleSystemPause(EntityId);
    public void Stop(bool clear = true) => ScriptBridge.ParticleSystemStop(EntityId, clear);
    public void Clear() => ScriptBridge.ParticleSystemClear(EntityId);
    public void Emit(int count) => ScriptBridge.ParticleSystemEmit(EntityId, count);
    public void EmitAt(Vector3 worldPosition, int count) => ScriptBridge.ParticleSystemEmitAt(EntityId, worldPosition, count);
}
