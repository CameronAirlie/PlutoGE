#pragma once

#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/scene/ui/UIEvents.h"

#include <vector>

namespace PlutoGE::scene
{
    // Makes an element draggable for scripts. It takes the drag once the
    // pointer passes the drag threshold and records BeginDrag, Drag (each
    // pointer move) and EndDrag (related: the drop target that accepted it,
    // or 0) with the pointer position. Presses and clicks pass through, so a
    // button on the same element still clicks when the pointer does not move.
    class UIDragSourceComponent : public TypedComponent<UIDragSourceComponent>, public IUIEventHandler
    {
    public:
        void Update(float) override {}
        bool RequiresFrameUpdate() const override { return false; }
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        void OnUIEvent(UIEvent &event, UIEventContext &context) override;

        [[nodiscard]] bool IsInteractable() const { return m_interactable; }
        void SetInteractable(bool value) { m_interactable = value; }
        [[nodiscard]] bool IsDragging() const { return m_dragging; }

    private:
        bool m_interactable = true;
        bool m_dragging = false;
    };

    // Accepts drops: a drag released over the element (or a descendant)
    // records Drop with the drag source as the related element.
    class UIDropTargetComponent : public TypedComponent<UIDropTargetComponent>, public IUIEventHandler
    {
    public:
        void Update(float) override {}
        bool RequiresFrameUpdate() const override { return false; }
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        void OnUIEvent(UIEvent &event, UIEventContext &context) override;

        [[nodiscard]] bool IsInteractable() const { return m_interactable; }
        void SetInteractable(bool value) { m_interactable = value; }

    private:
        bool m_interactable = true;
    };
}
