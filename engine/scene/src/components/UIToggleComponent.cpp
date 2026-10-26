#include "PlutoGE/scene/components/UIToggleComponent.h"

#include "PlutoGE/scene/components/UIComponent.h"
#include "../ui/UIControlHelpers.h"
#include "../ui/UIPropertyCodec.h"

namespace PlutoGE::scene
{
    using namespace uiprops;

    namespace
    {
        void CollectToggles(Entity &entity, std::vector<UIToggleComponent *> &toggles)
        {
            if (auto *toggle = entity.GetComponent<UIToggleComponent>())
                toggles.push_back(toggle);
            for (auto *child : entity.GetChildren())
                CollectToggles(*child, toggles);
        }
    }

    void UIToggleComponent::Toggle(UIEventContext &context)
    {
        auto *groupEntity = m_group != 0 ? context.FindEntity(m_group) : nullptr;
        const auto *group = groupEntity ? groupEntity->GetComponent<UIToggleGroupComponent>() : nullptr;
        if (m_isOn && group && !group->GetAllowSwitchOff())
            return; // a group keeps one toggle on
        m_isOn = !m_isOn;
        if (m_isOn && groupEntity)
        {
            // Members are the toggles under the group entity that name it.
            std::vector<UIToggleComponent *> toggles;
            CollectToggles(*groupEntity, toggles);
            for (auto *other : toggles)
                if (other != this && other->m_group == m_group && other->m_isOn)
                {
                    other->m_isOn = false;
                    context.Emit(UIEventType::ValueChanged, uicontrols::OwnerId(*other));
                }
        }
        context.Emit(UIEventType::ValueChanged, uicontrols::OwnerId(*this));
    }

    void UIToggleComponent::OnUIEvent(UIEvent &event, UIEventContext &context)
    {
        if (!IsInteractable())
            return;
        switch (event.type)
        {
        case UIEventType::PointerDown:
            event.Consume();
            break;
        case UIEventType::Click:
        case UIEventType::Submit:
            Toggle(context);
            event.Consume();
            break;
        default:
            break;
        }
    }

    void UIToggleComponent::DriveRects(UIEventContext &context)
    {
        // Opacity rather than the active flag, so the checkmark keeps its layout slot.
        if (auto *rect = uicontrols::Find<RectTransformComponent>(context, m_graphic))
            rect->SetOpacity(m_isOn ? 1.0f : 0.0f);
    }

    std::vector<Property> UIToggleComponent::Serialize() const
    {
        std::vector<Property> properties{BoolProperty("Enabled", IsEnabled()),
                                         BoolProperty("IsOn", m_isOn),
                                         EntityProperty("Graphic", m_graphic),
                                         EntityProperty("Group", m_group)};
        AppendSelectableProperties(properties);
        return properties;
    }

    void UIToggleComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Enabled") SetEnabled(ReadBool(property.value));
            else if (property.name == "IsOn") m_isOn = ReadBool(property.value);
            else if (property.name == "Graphic") m_graphic = ReadEntity(property.value);
            else if (property.name == "Group") m_group = ReadEntity(property.value);
            else ReadSelectableProperty(property);
        }
    }

    std::vector<Property> UIToggleGroupComponent::Serialize() const
    {
        return {BoolProperty("Enabled", IsEnabled()), BoolProperty("AllowSwitchOff", m_allowSwitchOff)};
    }

    void UIToggleGroupComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Enabled") SetEnabled(ReadBool(property.value));
            else if (property.name == "AllowSwitchOff") m_allowSwitchOff = ReadBool(property.value);
        }
    }
}
