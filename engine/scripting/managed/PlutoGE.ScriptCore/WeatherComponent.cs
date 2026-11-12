namespace PlutoGE.ScriptCore;

/// <summary>
/// The scene's weather state (docs/WEATHER.md). It holds state, not a forecast: a weather script changes it over time,
/// alongside the clouds (<see cref="VolumetricCloudComponent"/>), the wind (<see cref="WindComponent"/>) and rain
/// particles. The first enabled weather component on an active entity drives the scene; without one it is dry.
/// </summary>
[NativeComponent("WeatherComponent")]
public sealed class WeatherComponent : RegisteredComponent
{
    internal WeatherComponent(uint entityId) : base(entityId)
    {
    }

    /// <summary>
    /// How rain-soaked exposed surfaces look, 0 dry to 1 soaked: porous surfaces darken and every surface turns
    /// glossier, most where it faces the sky. Raise it while it rains and let it fall slowly after.
    /// </summary>
    public float Wetness { get => GetFloat("Wetness"); set => SetFloat("Wetness", value); }

    /// <summary>How hard it is raining, 0 to 1. Not drawn by the engine: rain particles, audio and gameplay read it.</summary>
    public float Precipitation { get => GetFloat("Precipitation"); set => SetFloat("Precipitation", value); }
}
