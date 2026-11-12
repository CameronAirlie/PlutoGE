#pragma once

namespace PlutoGE::render
{
    // The scene's weather as the renderer and other consumers read it, authored on a WeatherComponent and carried on
    // BasicLighting. Dry by default, which leaves rendering unchanged. A game's weather system drives it (and the
    // clouds, wind and particles) over time; the engine only draws the state it is given.
    struct SceneWeatherSettings
    {
        // How rain-soaked exposed surfaces are, 0 dry to 1 soaked: porous surfaces darken, every surface turns
        // glossier, most where it faces the sky (SurfaceWetness.slang).
        float wetness = 0.0f;
        // How hard it is raining (or snowing), 0 none to 1 a downpour. Not drawn by the renderer: precipitation
        // particles, audio and gameplay read it.
        float precipitation = 0.0f;

        bool Dry() const { return !(wetness > 0.0f); }
        bool operator==(const SceneWeatherSettings &) const = default;
    };
}
