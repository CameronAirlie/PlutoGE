#include "PlutoGE/scene/components/UIDragDropComponents.h"

#include "../ui/UIControlHelpers.h"
#include "../ui/UIPropertyCodec.h"

namespace PlutoGE::scene
{
    using namespace uiprops;

    void UIDragSourceComponent::OnUIEvent(UIEvent &event, UIEventContext &context)
    {
        const UIEntityID owner = uicontrols::OwnerId(*this);
        switch (event.type)
        {
        case UIEventType::BeginDrag:
            if (!m_interactable)
                return;
            m_dragging = true;
            context.Emit(UIEventType::BeginDrag, owner, event.position, 0);
            event.Consume(); // become the drag handler
            break;
        case UIEventType::Drag:
            // A drag that started keeps reporting even if made non-interactable meanwhile.
            if (!m_dragging)
                return;
            context.Emit(UIEventType::Drag, owner, event.position, 0);
            event.Consume();
            break;
        case UIEventType::EndDrag:
            if (!m_dragging)
                return;
            m_dragging = false;
            context.Emit(UIEventType::EndDrag, owner, event.position, event.related);
            event.Consume();
            break;
        default:
            break; // PointerDown and Click pass through (a button here still clicks)
        }
    }

    std::vector<Property> UIDragSourceComponent::Serialize() const
    {
        return {BoolProperty("Enabled", IsEnabled()), BoolProperty("Interactable", m_interactable)};
    }

    void UIDragSourceComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Enabled") SetEnabled(ReadBool(property.value));
            else if (property.name == "Interactable") m_interactable = ReadBool(property.value);
        }
    }

    void UIDropTargetComponent::OnUIEvent(UIEvent &event, UIEventContext &context)
    {
        if (event.type != UIEventType::Drop || !m_interactable)
            return;
        context.Emit(UIEventType::Drop, uicontrols::OwnerId(*this), event.position, event.related);
        event.Consume();
    }

    std::vector<Property> UIDropTargetComponent::Serialize() const
    {
        return {BoolProperty("Enabled", IsEnabled()), BoolProperty("Interactable", m_interactable)};
    }

    void UIDropTargetComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Enabled") SetEnabled(ReadBool(property.value));
            else if (property.name == "Interactable") m_interactable = ReadBool(property.value);
        }
    }
}
