#pragma once

#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/scene/ui/UISelectable.h"

#include <vector>

namespace PlutoGE::scene
{
    // An on/off control. Its checkmark graphic (any element) is shown while
    // on. Toggles naming the same group behave as radio buttons.
    class UIToggleComponent : public TypedComponent<UIToggleComponent>, public UISelectable, public IUIRectDriver
    {
    public:
        void Update(float) override {}
        bool RequiresFrameUpdate() const override { return false; }
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        void OnUIEvent(UIEvent &event, UIEventContext &context) override;
        void DriveRects(UIEventContext &context) override;

        [[nodiscard]] bool IsOn() const { return m_isOn; }
        // Sets the value without group rules or events (scripts, inspector).
        void SetIsOn(bool value) { m_isOn = value; }
        [[nodiscard]] UIEntityID GetGraphic() const { return m_graphic; }
        void SetGraphic(UIEntityID value) { m_graphic = value; }
        [[nodiscard]] UIEntityID GetGroup() const { return m_group; }
        void SetGroup(UIEntityID value) { m_group = value; }

    private:
        void Toggle(UIEventContext &context);

        bool m_isOn = false;
        UIEntityID m_graphic = 0;
        UIEntityID m_group = 0; // entity with a UIToggleGroupComponent
    };

    // Groups toggles that name it: at most one is on.
    class UIToggleGroupComponent : public TypedComponent<UIToggleGroupComponent>
    {
    public:
        void Update(float) override {}
        bool RequiresFrameUpdate() const override { return false; }
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        [[nodiscard]] bool GetAllowSwitchOff() const { return m_allowSwitchOff; }
        void SetAllowSwitchOff(bool value) { m_allowSwitchOff = value; }

    private:
        bool m_allowSwitchOff = false;
    };
}
