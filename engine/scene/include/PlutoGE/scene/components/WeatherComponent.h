#pragma once

#include "PlutoGE/render/SceneWeather.h"
#include "PlutoGE/scene/components/Component.h"

#include <algorithm>

namespace PlutoGE::scene
{
    // The scene's weather state: one setting every weather consumer shares. The first enabled WeatherComponent on an
    // active entity drives the scene; without one the scene is dry. It holds state, not a forecast: a game script
    // changes it over time (and drives clouds, wind and rain particles alongside). The owning entity's transform is
    // ignored. See docs/WEATHER.md.
    class WeatherComponent : public TypedComponent<WeatherComponent>
    {
    public:
        void Update(float) override {}
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        float GetWetness() const { return m_wetness; }
        float GetPrecipitation() const { return m_precipitation; }
        void SetWetness(float wetness) { m_wetness = std::clamp(wetness, 0.0f, 1.0f); }
        void SetPrecipitation(float precipitation) { m_precipitation = std::clamp(precipitation, 0.0f, 1.0f); }

        render::SceneWeatherSettings GetSettings() const { return {m_wetness, m_precipitation}; }

    private:
        float m_wetness = 0.0f;
        float m_precipitation = 0.0f;
    };
}
