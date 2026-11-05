namespace PlutoGE.ScriptCore;

/// <summary>
/// The scene's wind (docs/FOLIAGE_WIND.md). Foliage types with wind enabled bend with it every frame, shadows
/// included, so a weather script can drive a storm by raising <see cref="Speed"/> and <see cref="Gustiness"/>.
/// The first enabled wind component on an active entity drives the scene; without one the air is calm.
/// </summary>
[NativeComponent("WindComponent")]
public sealed class WindComponent : RegisteredComponent
{
    internal WindComponent(uint entityId) : base(entityId)
    {
    }

    /// <summary>Compass angle the wind blows toward, in degrees: 0 is +X, 90 is +Z.</summary>
    public float Direction { get => GetFloat("Direction"); set => SetFloat("Direction", value); }
    /// <summary>Mean wind speed in metres per second, 0 to 40. Foliage bends furthest from 20 m/s.</summary>
    public float Speed { get => GetFloat("Speed"); set => SetFloat("Speed", value); }
    /// <summary>0 is a steady wind; 1 swings between calm and twice the mean strength.</summary>
    public float Gustiness { get => GetFloat("Gustiness"); set => SetFloat("Gustiness", value); }
    /// <summary>Metres between gust fronts, which travel downwind at the wind speed.</summary>
    public float GustSize { get => GetFloat("Gust Size"); set => SetFloat("Gust Size", value); }
}
