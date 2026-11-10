#include "PlutoGE/scene/components/OutlineComponent.h"

#include <cmath>
#include <cstdio>
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

    void OutlineComponent::SetColor(const glm::vec4 &color)
    {
        if (!std::isfinite(color.r) || !std::isfinite(color.g) || !std::isfinite(color.b) || !std::isfinite(color.a))
            return;
        m_color = glm::vec4(std::max(color.r, 0.0f), std::max(color.g, 0.0f), std::max(color.b, 0.0f),
                            std::clamp(color.a, 0.0f, 1.0f));
    }

    void OutlineComponent::SetWidth(float width)
    {
        if (std::isfinite(width))
            m_width = std::clamp(width, MinWidth, MaxWidth);
    }

    void OutlineComponent::SetHiddenOpacity(float opacity)
    {
        if (std::isfinite(opacity))
            m_hiddenOpacity = std::clamp(opacity, 0.0f, 1.0f);
    }

    std::vector<Property> OutlineComponent::Serialize() const
    {
        const std::string color = std::to_string(m_color.r) + "," + std::to_string(m_color.g) + "," +
                                  std::to_string(m_color.b) + "," + std::to_string(m_color.a);
        return {
            {"Color", PropertyType::Color, color},
            {"Width", PropertyType::Float, std::to_string(m_width)},
            {"Hidden Opacity", PropertyType::Float, std::to_string(m_hiddenOpacity)},
            {"Include Children", PropertyType::Bool, m_includeChildren ? "true" : "false"},
        };
    }

    void OutlineComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Color")
            {
                glm::vec4 color = m_color;
                if (std::sscanf(property.value.c_str(), "%f,%f,%f,%f", &color.r, &color.g, &color.b, &color.a) >= 3)
                    SetColor(color);
            }
            else if (property.name == "Width")
                SetWidth(ParseFloat(property.value, m_width));
            else if (property.name == "Hidden Opacity")
                SetHiddenOpacity(ParseFloat(property.value, m_hiddenOpacity));
            else if (property.name == "Include Children")
                m_includeChildren = property.value == "true" || property.value == "1";
        }
    }
}
