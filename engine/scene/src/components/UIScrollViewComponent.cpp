#include "PlutoGE/scene/components/UIScrollViewComponent.h"

#include "PlutoGE/scene/components/UIComponent.h"
#include "../ui/UIControlHelpers.h"
#include "../ui/UIPropertyCodec.h"

#include <cmath>

namespace PlutoGE::scene
{
    using namespace uiprops;

    namespace
    {
        // Overscroll resistance while dragging past an end.
        float RubberBand(float overStretch, float viewSize)
        {
            if (viewSize <= 0.0f)
                return 0.0f;
            return (1.0f - 1.0f / (std::abs(overStretch) * 0.55f / viewSize + 1.0f)) * viewSize * (overStretch < 0.0f ? -1.0f : 1.0f);
        }
    }

    glm::vec2 UIScrollViewComponent::Clamp(glm::vec2 scroll) const
    {
        return glm::clamp(scroll, glm::vec2(0.0f), m_maxScroll);
    }

    glm::vec2 UIScrollViewComponent::GetNormalizedPosition() const
    {
        return {m_maxScroll.x > 0.0f ? m_scroll.x / m_maxScroll.x : 0.0f, m_maxScroll.y > 0.0f ? m_scroll.y / m_maxScroll.y : 0.0f};
    }

    void UIScrollViewComponent::SetNormalizedPosition(glm::vec2 value)
    {
        SetScrollPosition(glm::clamp(value, glm::vec2(0.0f), glm::vec2(1.0f)) * m_maxScroll);
    }

    void UIScrollViewComponent::DriveRects(UIEventContext &context)
    {
        const ui::Rect *view = uicontrols::OwnerRect(context, *this);
        const auto *content = context.FindElement(m_content);
        if (view && content)
            m_maxScroll = glm::max(content->localRect.Size() - view->Size(), glm::vec2(0.0f)) * AxisMask();
        if (m_movement == UIScrollMovement::Clamped)
            m_scroll = Clamp(m_scroll);
        m_scroll *= AxisMask();
        if (auto *rect = uicontrols::Find<RectTransformComponent>(context, m_content))
            rect->SetAnchoredPosition({-m_scroll.x, m_scroll.y});
    }

    void UIScrollViewComponent::OnUIEvent(UIEvent &event, UIEventContext &context)
    {
        const UIEntityID owner = uicontrols::OwnerId(*this);
        switch (event.type)
        {
        case UIEventType::Scroll:
        {
            glm::vec2 delta = event.scroll * m_sensitivity;
            if (m_horizontal && !m_vertical)
                delta = {delta.x - delta.y, 0.0f}; // a horizontal view scrolls with the wheel too
            m_scroll += glm::vec2(delta.x, -delta.y) * AxisMask();
            if (m_movement != UIScrollMovement::Unrestricted)
                m_scroll = Clamp(m_scroll);
            m_velocity = glm::vec2(0.0f);
            event.Consume();
            break;
        }
        case UIEventType::BeginDrag:
            m_dragging = true;
            m_dragStartScroll = m_scroll;
            m_dragStartLocal = context.ToLocal(owner, event.position);
            m_velocity = glm::vec2(0.0f);
            event.Consume();
            break;
        case UIEventType::Drag:
        {
            const glm::vec2 moved = context.ToLocal(owner, event.position) - m_dragStartLocal;
            glm::vec2 target = m_dragStartScroll + glm::vec2(-moved.x, moved.y) * AxisMask();
            if (m_movement == UIScrollMovement::Elastic)
            {
                const ui::Rect *view = uicontrols::OwnerRect(context, *this);
                const glm::vec2 viewSize = view ? view->Size() : glm::vec2(0.0f);
                const glm::vec2 clamped = Clamp(target);
                for (int axis = 0; axis < 2; ++axis)
                    if (target[axis] != clamped[axis])
                        target[axis] = clamped[axis] + RubberBand(target[axis] - clamped[axis], viewSize[axis]);
            }
            else if (m_movement == UIScrollMovement::Clamped)
                target = Clamp(target);
            const float dt = std::max(context.GetDeltaTime(), 1e-4f);
            m_velocity = glm::mix(m_velocity, (target - m_scroll) / dt, 0.5f);
            m_scroll = target;
            event.Consume();
            break;
        }
        case UIEventType::EndDrag:
            m_dragging = false;
            if (!m_inertia)
                m_velocity = glm::vec2(0.0f);
            event.Consume();
            break;
        default:
            break;
        }
    }

    void UIScrollViewComponent::UpdateUI(UIEventContext &, float deltaTime)
    {
        if (m_dragging || deltaTime <= 0.0f)
            return;
        if (m_inertia && glm::length(m_velocity) > 1.0f)
        {
            m_scroll += m_velocity * deltaTime * AxisMask();
            m_velocity *= std::pow(m_decelerationRate, deltaTime);
        }
        else
            m_velocity = glm::vec2(0.0f);

        const glm::vec2 clamped = Clamp(m_scroll);
        if (m_movement == UIScrollMovement::Clamped)
            m_scroll = clamped;
        else if (m_movement == UIScrollMovement::Elastic && clamped != m_scroll)
        {
            // Spring back to the nearest end; overscroll also stops inertia on that axis.
            const float keep = std::exp(-deltaTime / std::max(m_elasticity, 1e-3f) * 4.0f);
            for (int axis = 0; axis < 2; ++axis)
                if (clamped[axis] != m_scroll[axis])
                {
                    m_scroll[axis] = clamped[axis] + (m_scroll[axis] - clamped[axis]) * keep;
                    m_velocity[axis] = 0.0f;
                    if (std::abs(m_scroll[axis] - clamped[axis]) < 0.1f)
                        m_scroll[axis] = clamped[axis];
                }
        }
    }

    bool UIScrollViewComponent::GetRuntimeProperty(std::string_view name, std::string &value) const
    {
        if (name == "ScrollPosition") value = FormatVector(m_scroll);
        else if (name == "NormalizedPosition") value = FormatVector(GetNormalizedPosition());
        else return false;
        return true;
    }

    bool UIScrollViewComponent::SetRuntimeProperty(std::string_view name, std::string_view value)
    {
        if (name == "ScrollPosition") SetScrollPosition(ReadVector(value, m_scroll));
        else if (name == "NormalizedPosition") SetNormalizedPosition(ReadVector(value, GetNormalizedPosition()));
        else return false;
        return true;
    }

    std::vector<Property> UIScrollViewComponent::Serialize() const
    {
        return {
            BoolProperty("Enabled", IsEnabled()),
            EntityProperty("Content", m_content),
            BoolProperty("Horizontal", m_horizontal),
            BoolProperty("Vertical", m_vertical),
            EnumProperty("Movement", m_movement, {"Unrestricted", "Elastic", "Clamped"}),
            FloatProperty("Elasticity", m_elasticity),
            BoolProperty("Inertia", m_inertia),
            FloatProperty("DecelerationRate", m_decelerationRate),
            FloatProperty("ScrollSensitivity", m_sensitivity),
        };
    }

    void UIScrollViewComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            const auto &name = property.name;
            const auto &value = property.value;
            if (name == "Enabled") SetEnabled(ReadBool(value));
            else if (name == "Content") m_content = ReadEntity(value);
            else if (name == "Horizontal") m_horizontal = ReadBool(value);
            else if (name == "Vertical") m_vertical = ReadBool(value);
            else if (name == "Movement") m_movement = ReadEnum(value, UIScrollMovement::Clamped, m_movement);
            else if (name == "Elasticity") m_elasticity = std::max(ReadFloat(value, m_elasticity), 0.0f);
            else if (name == "Inertia") m_inertia = ReadBool(value);
            else if (name == "DecelerationRate") m_decelerationRate = std::clamp(ReadFloat(value, m_decelerationRate), 0.0f, 1.0f);
            else if (name == "ScrollSensitivity") m_sensitivity = ReadFloat(value, m_sensitivity);
        }
    }
}
