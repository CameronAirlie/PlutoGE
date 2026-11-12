#include "PlutoGE/scene/components/WeatherComponent.h"

#include <cmath>
#include <string>

namespace PlutoGE::scene
{
    namespace
    {
        // Malformed or non-finite text keeps the current value instead of throwing out of scene loading.
        float ParseFloat(const std::string &text, float fallback)
        {
            try
            {
                const float value = std::stof(text);
                return std::isfinite(value) ? value : fallback;
            }
            catch (const std::exception &)
            {
                return fallback;
            }
        }
    }

    std::vector<Property> WeatherComponent::Serialize() const
    {
        return {
            {"Wetness", PropertyType::Float, std::to_string(m_wetness)},
            {"Precipitation", PropertyType::Float, std::to_string(m_precipitation)},
        };
    }

    void WeatherComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Wetness") SetWetness(ParseFloat(property.value, m_wetness));
            else if (property.name == "Precipitation") SetPrecipitation(ParseFloat(property.value, m_precipitation));
        }
    }
}
