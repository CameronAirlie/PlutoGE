#include "PlutoGE/scene/ui/UISelectable.h"

#include "UIPropertyCodec.h"

#include <cmath>

namespace PlutoGE::scene
{
    using namespace uiprops;

    namespace
    {
        // Property names match the original UIButtonComponent so older scenes load unchanged.
        constexpr const char *kTintNames[] = {"NormalTint", "HoveredTint", "PressedTint", "SelectedTint", "DisabledTint"};
        constexpr const char *kNeighbourNames[] = {"NavigateUp", "NavigateDown", "NavigateLeft", "NavigateRight"};
    }

    UISelectableState UISelectable::GetState() const
    {
        if (!m_interactable) return UISelectableState::Disabled;
        if (m_pressed) return UISelectableState::Pressed;
        if (m_hovered) return UISelectableState::Highlighted;
        if (m_selected) return UISelectableState::Selected;
        return UISelectableState::Normal;
    }

    void UISelectable::UpdateTransition(float deltaTime)
    {
        const glm::vec4 target = GetStateTint(GetState());
        // Exponential approach reaching ~99% of the target after the duration.
        const float blend = m_transitionDuration <= 0.0001f
                                ? 1.0f
                                : 1.0f - std::exp(-std::max(deltaTime, 0.0f) * 4.6f / m_transitionDuration);
        m_currentTint = glm::mix(m_currentTint, target, glm::clamp(blend, 0.0f, 1.0f));
    }

    void UISelectable::SnapTransition()
    {
        m_currentTint = GetStateTint(GetState());
    }

    void UISelectable::AppendSelectableProperties(std::vector<Property> &properties) const
    {
        properties.push_back(BoolProperty("Interactable", m_interactable));
        for (std::size_t index = 0; index < m_tints.size(); ++index)
            properties.push_back(ColorProperty(kTintNames[index], m_tints[index]));
        properties.push_back(FloatProperty("TransitionDuration", m_transitionDuration));
        properties.push_back(EnumProperty("Navigation", m_navigation, {"Automatic", "Explicit", "None"}));
        for (std::size_t index = 0; index < m_neighbours.size(); ++index)
            properties.push_back(EntityProperty(kNeighbourNames[index], m_neighbours[index]));
    }

    bool UISelectable::ReadSelectableProperty(const Property &property)
    {
        const auto &name = property.name;
        if (name == "Interactable")
        {
            m_interactable = ReadBool(property.value);
            return true;
        }
        for (std::size_t index = 0; index < m_tints.size(); ++index)
            if (name == kTintNames[index])
            {
                m_tints[index] = ReadVector(property.value, m_tints[index]);
                return true;
            }
        if (name == "TransitionDuration")
        {
            SetTransitionDuration(ReadFloat(property.value, m_transitionDuration));
            return true;
        }
        if (name == "Navigation")
        {
            m_navigation = ReadEnum(property.value, UINavigationMode::None, m_navigation);
            return true;
        }
        for (std::size_t index = 0; index < m_neighbours.size(); ++index)
            if (name == kNeighbourNames[index])
            {
                m_neighbours[index] = ReadEntity(property.value);
                return true;
            }
        return false;
    }
}
