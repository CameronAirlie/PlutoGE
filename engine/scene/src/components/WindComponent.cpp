#include "PlutoGE/scene/components/WindComponent.h"

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

    void WindComponent::SetDirectionDegrees(float degrees)
    {
        if (!std::isfinite(degrees))
            return;
        m_directionDegrees = std::fmod(degrees, 360.0f);
        if (m_directionDegrees < 0.0f)
            m_directionDegrees += 360.0f;
    }

    render::SceneWindSettings WindComponent::GetSettings() const
    {
        render::SceneWindSettings settings;
        settings.direction = render::SceneWindDirectionFromDegrees(m_directionDegrees);
        settings.speed = m_speed;
        settings.gustiness = m_gustiness;
        settings.gustSize = m_gustSize;
        return settings;
    }

    std::vector<Property> WindComponent::Serialize() const
    {
        return {
            {"Direction", PropertyType::Float, std::to_string(m_directionDegrees)},
            {"Speed", PropertyType::Float, std::to_string(m_speed)},
            {"Gustiness", PropertyType::Float, std::to_string(m_gustiness)},
            {"Gust Size", PropertyType::Float, std::to_string(m_gustSize)},
        };
    }

    void WindComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Direction") SetDirectionDegrees(ParseFloat(property.value, m_directionDegrees));
            else if (property.name == "Speed") SetSpeed(ParseFloat(property.value, m_speed));
            else if (property.name == "Gustiness") SetGustiness(ParseFloat(property.value, m_gustiness));
            else if (property.name == "Gust Size") SetGustSize(ParseFloat(property.value, m_gustSize));
        }
    }
}
