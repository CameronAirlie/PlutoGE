#pragma once

#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/scene/ui/UISelectable.h"

#include <vector>

namespace PlutoGE::scene
{
    enum class UISliderDirection : std::uint8_t
    {
        LeftToRight,
        RightToLeft,
        BottomToTop,
        TopToBottom,
    };

    // A value between Min and Max chosen by dragging or with navigation along
    // its axis. It anchors its fill and handle elements to the value.
    class UISliderComponent : public TypedComponent<UISliderComponent>, public UISelectable, public IUIRectDriver
    {
    public:
        void Update(float) override {}
        bool RequiresFrameUpdate() const override { return false; }
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        void OnUIEvent(UIEvent &event, UIEventContext &context) override;
        bool HandleMove(glm::ivec2 direction, UIEventContext &context) override;
        void DriveRects(UIEventContext &context) override;

        [[nodiscard]] float GetValue() const { return m_value; }
        // Clamped to the range (and rounded for whole numbers).
        void SetValue(float value);
        [[nodiscard]] float GetNormalizedValue() const;
        void SetNormalizedValue(float value);
        [[nodiscard]] float GetMinimum() const { return m_minimum; }
        [[nodiscard]] float GetMaximum() const { return m_maximum; }
        void SetRange(float minimum, float maximum);
        [[nodiscard]] bool GetWholeNumbers() const { return m_wholeNumbers; }
        void SetWholeNumbers(bool value) { m_wholeNumbers = value; SetValue(m_value); }
        [[nodiscard]] UISliderDirection GetDirection() const { return m_direction; }
        void SetDirection(UISliderDirection value) { m_direction = value; }
        [[nodiscard]] UIEntityID GetFill() const { return m_fill; }
        void SetFill(UIEntityID value) { m_fill = value; }
        [[nodiscard]] UIEntityID GetHandle() const { return m_handle; }
        void SetHandle(UIEntityID value) { m_handle = value; }

    private:
        [[nodiscard]] bool IsHorizontal() const;
        [[nodiscard]] bool IsReversed() const;
        void SetFromPointer(glm::vec2 pixel, UIEventContext &context);
        void Change(float value, UIEventContext &context);

        float m_minimum = 0.0f;
        float m_maximum = 1.0f;
        float m_value = 0.0f;
        bool m_wholeNumbers = false;
        UISliderDirection m_direction = UISliderDirection::LeftToRight;
        UIEntityID m_fill = 0;
        UIEntityID m_handle = 0;
    };
}
