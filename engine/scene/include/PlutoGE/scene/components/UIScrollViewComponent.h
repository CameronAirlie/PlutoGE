#pragma once

#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/scene/ui/UIEvents.h"

#include <glm/glm.hpp>

#include <vector>

namespace PlutoGE::scene
{
    enum class UIScrollMovement : std::uint8_t
    {
        Unrestricted,
        Elastic,  // may be dragged past the ends, then springs back
        Clamped,
    };

    // Scrolls its content element inside its own rect with the wheel and
    // by dragging, with inertia. Enable ClipChildren on the same
    // RectTransform to hide content outside the view.
    class UIScrollViewComponent : public TypedComponent<UIScrollViewComponent>,
                                  public IUIEventHandler,
                                  public IUIRectDriver,
                                  public IUIUpdatable,
                                  public IUIRuntimeProperties
    {
    public:
        void Update(float) override {}
        bool RequiresFrameUpdate() const override { return false; }
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        void OnUIEvent(UIEvent &event, UIEventContext &context) override;
        void DriveRects(UIEventContext &context) override;
        void UpdateUI(UIEventContext &context, float deltaTime) override;
        // "ScrollPosition" and "NormalizedPosition" as "x,y".
        bool GetRuntimeProperty(std::string_view name, std::string &value) const override;
        bool SetRuntimeProperty(std::string_view name, std::string_view value) override;

        // Scroll offset in canvas units: x to the right, y downwards.
        [[nodiscard]] glm::vec2 GetScrollPosition() const { return m_scroll; }
        void SetScrollPosition(glm::vec2 value) { m_scroll = value; m_velocity = glm::vec2(0.0f); }
        // 0 at the start (top/left), 1 at the end; zero when nothing to scroll.
        [[nodiscard]] glm::vec2 GetNormalizedPosition() const;
        void SetNormalizedPosition(glm::vec2 value);
        [[nodiscard]] glm::vec2 GetMaxScroll() const { return m_maxScroll; }

        [[nodiscard]] UIEntityID GetContent() const { return m_content; }
        void SetContent(UIEntityID value) { m_content = value; }
        [[nodiscard]] bool GetHorizontal() const { return m_horizontal; }
        void SetHorizontal(bool value) { m_horizontal = value; }
        [[nodiscard]] bool GetVertical() const { return m_vertical; }
        void SetVertical(bool value) { m_vertical = value; }
        [[nodiscard]] UIScrollMovement GetMovement() const { return m_movement; }
        void SetMovement(UIScrollMovement value) { m_movement = value; }
        [[nodiscard]] float GetScrollSensitivity() const { return m_sensitivity; }
        void SetScrollSensitivity(float value) { m_sensitivity = value; }

    private:
        [[nodiscard]] glm::vec2 Clamp(glm::vec2 scroll) const;
        [[nodiscard]] glm::vec2 AxisMask() const { return {m_horizontal ? 1.0f : 0.0f, m_vertical ? 1.0f : 0.0f}; }

        UIEntityID m_content = 0;
        bool m_horizontal = false;
        bool m_vertical = true;
        UIScrollMovement m_movement = UIScrollMovement::Elastic;
        float m_elasticity = 0.1f;      // seconds to spring back
        bool m_inertia = true;
        float m_decelerationRate = 0.135f; // velocity kept per second
        float m_sensitivity = 30.0f;    // canvas units per wheel step

        glm::vec2 m_scroll{0.0f};
        glm::vec2 m_velocity{0.0f};
        glm::vec2 m_maxScroll{0.0f};
        bool m_dragging = false;
        glm::vec2 m_dragStartScroll{0.0f};
        glm::vec2 m_dragStartLocal{0.0f};
    };
}
