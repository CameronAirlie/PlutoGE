#include "PlutoGE/scene/components/WaterInteractorComponent.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace PlutoGE::scene
{
    namespace
    {
        std::string Vec3Text(const glm::vec3 &value)
        {
            return std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z);
        }

        float ParseFloat(const std::string &text, float fallback)
        {
            char *end = nullptr;
            const float value = std::strtof(text.c_str(), &end);
            return end != text.c_str() && std::isfinite(value) ? value : fallback;
        }

        const char *BoolText(bool value) { return value ? "true" : "false"; }
        bool ParseBool(const std::string &text) { return text == "true" || text == "True" || text == "1"; }
    }

    void WaterInteractorComponent::SetRadius(float radius)
    {
        m_radius = std::isfinite(radius) ? std::clamp(radius, 0.0f, 100.0f) : 0.0f;
    }

    void WaterInteractorComponent::SetEffectScale(float scale)
    {
        m_effectScale = std::isfinite(scale) ? std::clamp(scale, 0.0f, 10.0f) : 1.0f;
    }

    std::vector<Property> WaterInteractorComponent::Serialize() const
    {
        return {
            {"Radius", PropertyType::Float, std::to_string(m_radius)},
            {"CenterOffset", PropertyType::Vec3, Vec3Text(m_centerOffset)},
            {"EffectScale", PropertyType::Float, std::to_string(m_effectScale)},
            {"Ripples", PropertyType::Bool, BoolText(m_channels.ripples)},
            {"Splashes", PropertyType::Bool, BoolText(m_channels.splashes)},
            {"Sounds", PropertyType::Bool, BoolText(m_channels.sounds)},
        };
    }

    void WaterInteractorComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            const auto &value = property.Text();
            if (property.name == "Radius")
                SetRadius(ParseFloat(value, m_radius));
            else if (property.name == "CenterOffset")
            {
                glm::vec3 offset = m_centerOffset;
                std::sscanf(value.c_str(), "%f,%f,%f", &offset.x, &offset.y, &offset.z);
                if (std::isfinite(offset.x) && std::isfinite(offset.y) && std::isfinite(offset.z))
                    m_centerOffset = offset;
            }
            else if (property.name == "EffectScale")
                SetEffectScale(ParseFloat(value, m_effectScale));
            else if (property.name == "Ripples")
                m_channels.ripples = ParseBool(value);
            else if (property.name == "Splashes")
                m_channels.splashes = ParseBool(value);
            else if (property.name == "Sounds")
                m_channels.sounds = ParseBool(value);
        }
    }
}
