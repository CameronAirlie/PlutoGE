#include "PlutoGE/scene/components/UIComponent.h"

#include <algorithm>
#include <cstdio>

namespace PlutoGE::scene
{
    namespace
    {
        std::string SerializeVec2(const glm::vec2 &value)
        {
            return std::to_string(value.x) + "," + std::to_string(value.y);
        }

        glm::vec2 ParseVec2(const std::string &value, const glm::vec2 &fallback = glm::vec2(0.0f))
        {
            glm::vec2 parsedValue = fallback;
            std::sscanf(value.c_str(), "%f,%f", &parsedValue.x, &parsedValue.y);
            return parsedValue;
        }

        std::string SerializeColor(const glm::vec4 &value)
        {
            return std::to_string(value.r) + "," + std::to_string(value.g) + "," + std::to_string(value.b) + "," + std::to_string(value.a);
        }

        glm::vec4 ParseColor(const std::string &value, const glm::vec4 &fallback = glm::vec4(1.0f))
        {
            glm::vec4 parsedValue = fallback;
            std::sscanf(value.c_str(), "%f,%f,%f,%f", &parsedValue.r, &parsedValue.g, &parsedValue.b, &parsedValue.a);
            return parsedValue;
        }

        const std::vector<std::string> &AnchorPresetNames()
        {
            static const std::vector<std::string> names = {
                "TopLeft",
                "TopCenter",
                "TopRight",
                "MiddleLeft",
                "MiddleCenter",
                "MiddleRight",
                "BottomLeft",
                "BottomCenter",
                "BottomRight",
                "Stretch",
            };
            return names;
        }

        int ClampAnchorPresetIndex(int index)
        {
            return std::clamp(index, 0, static_cast<int>(AnchorPresetNames().size()) - 1);
        }
    }

    std::vector<Property> CanvasComponent::Serialize() const
    {
        return {
            {"Enabled", PropertyType::Bool, IsEnabled() ? "true" : "false"},
            {"RenderMode", PropertyType::Enum, std::to_string(static_cast<int>(m_renderMode)), {"ScreenSpaceOverlay"}},
            {"ScaleFactor", PropertyType::Float, std::to_string(m_scaleFactor)},
            {"SortingOrder", PropertyType::Int, std::to_string(m_sortingOrder)},
        };
    }

    void CanvasComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Enabled")
                SetEnabled(property.value == "true" || property.value == "1");
            else if (property.name == "RenderMode")
                m_renderMode = CanvasRenderMode::ScreenSpaceOverlay;
            else if (property.name == "ScaleFactor")
                m_scaleFactor = std::stof(property.value);
            else if (property.name == "SortingOrder")
                m_sortingOrder = std::stoi(property.value);
        }
    }

    void RectTransformComponent::SetAnchorPreset(UIAnchorPreset preset)
    {
        m_anchorPreset = preset;
        switch (preset)
        {
        case UIAnchorPreset::TopLeft:
            m_anchorMin = m_anchorMax = glm::vec2(0.0f, 1.0f);
            m_pivot = glm::vec2(0.0f, 1.0f);
            break;
        case UIAnchorPreset::TopCenter:
            m_anchorMin = m_anchorMax = glm::vec2(0.5f, 1.0f);
            m_pivot = glm::vec2(0.5f, 1.0f);
            break;
        case UIAnchorPreset::TopRight:
            m_anchorMin = m_anchorMax = glm::vec2(1.0f, 1.0f);
            m_pivot = glm::vec2(1.0f, 1.0f);
            break;
        case UIAnchorPreset::MiddleLeft:
            m_anchorMin = m_anchorMax = glm::vec2(0.0f, 0.5f);
            m_pivot = glm::vec2(0.0f, 0.5f);
            break;
        case UIAnchorPreset::MiddleRight:
            m_anchorMin = m_anchorMax = glm::vec2(1.0f, 0.5f);
            m_pivot = glm::vec2(1.0f, 0.5f);
            break;
        case UIAnchorPreset::BottomLeft:
            m_anchorMin = m_anchorMax = glm::vec2(0.0f, 0.0f);
            m_pivot = glm::vec2(0.0f, 0.0f);
            break;
        case UIAnchorPreset::BottomCenter:
            m_anchorMin = m_anchorMax = glm::vec2(0.5f, 0.0f);
            m_pivot = glm::vec2(0.5f, 0.0f);
            break;
        case UIAnchorPreset::BottomRight:
            m_anchorMin = m_anchorMax = glm::vec2(1.0f, 0.0f);
            m_pivot = glm::vec2(1.0f, 0.0f);
            break;
        case UIAnchorPreset::Stretch:
            m_anchorMin = glm::vec2(0.0f);
            m_anchorMax = glm::vec2(1.0f);
            m_pivot = glm::vec2(0.5f);
            break;
        case UIAnchorPreset::MiddleCenter:
        default:
            m_anchorMin = m_anchorMax = glm::vec2(0.5f);
            m_pivot = glm::vec2(0.5f);
            break;
        }
    }

    std::vector<Property> RectTransformComponent::Serialize() const
    {
        return {
            {"Enabled", PropertyType::Bool, IsEnabled() ? "true" : "false"},
            {"AnchorPreset", PropertyType::Enum, std::to_string(static_cast<int>(m_anchorPreset)), AnchorPresetNames()},
            {"AnchoredPosition", PropertyType::Vec2, SerializeVec2(m_anchoredPosition)},
            {"SizeDelta", PropertyType::Vec2, SerializeVec2(m_sizeDelta)},
            {"Pivot", PropertyType::Vec2, SerializeVec2(m_pivot)},
            {"AnchorMin", PropertyType::Vec2, SerializeVec2(m_anchorMin)},
            {"AnchorMax", PropertyType::Vec2, SerializeVec2(m_anchorMax)},
        };
    }

    void RectTransformComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Enabled")
                SetEnabled(property.value == "true" || property.value == "1");
            else if (property.name == "AnchorPreset")
                m_anchorPreset = static_cast<UIAnchorPreset>(ClampAnchorPresetIndex(std::stoi(property.value)));
            else if (property.name == "AnchoredPosition")
                m_anchoredPosition = ParseVec2(property.value, m_anchoredPosition);
            else if (property.name == "SizeDelta")
                m_sizeDelta = ParseVec2(property.value, m_sizeDelta);
            else if (property.name == "Pivot")
                m_pivot = ParseVec2(property.value, m_pivot);
            else if (property.name == "AnchorMin")
                m_anchorMin = ParseVec2(property.value, m_anchorMin);
            else if (property.name == "AnchorMax")
                m_anchorMax = ParseVec2(property.value, m_anchorMax);
        }
    }

    std::vector<Property> UIImageComponent::Serialize() const
    {
        return {
            {"Enabled", PropertyType::Bool, IsEnabled() ? "true" : "false"},
            {"Color", PropertyType::Color, SerializeColor(m_color)},
            {"TexturePath", PropertyType::String, m_texturePath},
        };
    }

    void UIImageComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Enabled")
                SetEnabled(property.value == "true" || property.value == "1");
            else if (property.name == "Color")
                m_color = ParseColor(property.value, m_color);
            else if (property.name == "TexturePath")
                m_texturePath = property.value;
        }
    }

    std::vector<Property> UITextComponent::Serialize() const
    {
        return {
            {"Enabled", PropertyType::Bool, IsEnabled() ? "true" : "false"},
            {"Text", PropertyType::String, m_text},
            {"Color", PropertyType::Color, SerializeColor(m_color)},
            {"FontSize", PropertyType::Float, std::to_string(m_fontSize)},
            {"FontPath", PropertyType::String, m_fontPath},
        };
    }

    void UITextComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Enabled")
                SetEnabled(property.value == "true" || property.value == "1");
            else if (property.name == "Text")
                m_text = property.value;
            else if (property.name == "Color")
                m_color = ParseColor(property.value, m_color);
            else if (property.name == "FontSize")
                m_fontSize = std::stof(property.value);
            else if (property.name == "FontPath")
                m_fontPath = property.value;
        }
    }

    void UIButtonComponent::SetRuntimeState(bool hovered, bool pressed, bool released, bool clicked)
    {
        m_hovered = hovered;
        m_pressed = pressed;
        m_released = released;
        m_clicked = clicked;
    }

    std::vector<Property> UIButtonComponent::Serialize() const
    {
        return {
            {"Enabled", PropertyType::Bool, IsEnabled() ? "true" : "false"},
            {"Interactable", PropertyType::Bool, m_interactable ? "true" : "false"},
        };
    }

    void UIButtonComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Enabled")
                SetEnabled(property.value == "true" || property.value == "1");
            else if (property.name == "Interactable")
                m_interactable = property.value == "true" || property.value == "1";
        }
    }
}
