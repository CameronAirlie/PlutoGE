#include "PlutoGE/scene/components/UISliderComponent.h"

#include "PlutoGE/scene/components/UIComponent.h"
#include "../ui/UIControlHelpers.h"
#include "../ui/UIPropertyCodec.h"

#include <cmath>

namespace PlutoGE::scene
{
    using namespace uiprops;

    bool UISliderComponent::IsHorizontal() const
    {
        return m_direction == UISliderDirection::LeftToRight || m_direction == UISliderDirection::RightToLeft;
    }

    bool UISliderComponent::IsReversed() const
    {
        return m_direction == UISliderDirection::RightToLeft || m_direction == UISliderDirection::TopToBottom;
    }

    void UISliderComponent::SetValue(float value)
    {
        const float low = std::min(m_minimum, m_maximum), high = std::max(m_minimum, m_maximum);
        value = std::clamp(value, low, high);
        m_value = m_wholeNumbers ? std::round(value) : value;
    }

    void UISliderComponent::SetRange(float minimum, float maximum)
    {
        m_minimum = minimum;
        m_maximum = maximum;
        SetValue(m_value);
    }

    float UISliderComponent::GetNormalizedValue() const
    {
        const float range = m_maximum - m_minimum;
        return std::abs(range) < 1e-6f ? 0.0f : std::clamp((m_value - m_minimum) / range, 0.0f, 1.0f);
    }

    void UISliderComponent::SetNormalizedValue(float value)
    {
        SetValue(m_minimum + std::clamp(value, 0.0f, 1.0f) * (m_maximum - m_minimum));
    }

    void UISliderComponent::Change(float value, UIEventContext &context)
    {
        const float previous = m_value;
        SetValue(value);
        if (m_value != previous)
            context.Emit(UIEventType::ValueChanged, uicontrols::OwnerId(*this));
    }

    void UISliderComponent::SetFromPointer(glm::vec2 pixel, UIEventContext &context)
    {
        const ui::Rect *rect = uicontrols::OwnerRect(context, *this);
        if (!rect)
            return;
        const glm::vec2 local = context.ToLocal(uicontrols::OwnerId(*this), pixel);
        const int axis = IsHorizontal() ? 0 : 1;
        const float extent = rect->Size()[axis];
        float t = extent > 0.0f ? (local[axis] - rect->min[axis]) / extent : 0.0f;
        if (IsReversed())
            t = 1.0f - t;
        Change(m_minimum + std::clamp(t, 0.0f, 1.0f) * (m_maximum - m_minimum), context);
    }

    void UISliderComponent::OnUIEvent(UIEvent &event, UIEventContext &context)
    {
        if (!IsInteractable())
            return;
        switch (event.type)
        {
        case UIEventType::PointerDown:
        case UIEventType::Drag:
            SetFromPointer(event.position, context);
            event.Consume();
            break;
        case UIEventType::BeginDrag:
            event.Consume();
            break;
        default:
            break;
        }
    }

    bool UISliderComponent::HandleMove(glm::ivec2 direction, UIEventContext &context)
    {
        if (!IsInteractable())
            return false;
        const int step = IsHorizontal() ? direction.x : direction.y;
        if (step == 0)
            return false; // across the axis: navigate away
        const float increment = m_wholeNumbers ? 1.0f : (m_maximum - m_minimum) * 0.1f;
        Change(m_value + static_cast<float>(IsReversed() ? -step : step) * increment, context);
        return true;
    }

    void UISliderComponent::DriveRects(UIEventContext &context)
    {
        const float t = GetNormalizedValue();
        const bool horizontal = IsHorizontal();
        const bool reversed = IsReversed();
        if (auto *fill = uicontrols::Find<RectTransformComponent>(context, m_fill))
        {
            // The fill spans from the start edge to the value.
            const float start = reversed ? 1.0f - t : 0.0f, end = reversed ? 1.0f : t;
            fill->SetAnchorMin(horizontal ? glm::vec2(start, 0.0f) : glm::vec2(0.0f, start));
            fill->SetAnchorMax(horizontal ? glm::vec2(end, 1.0f) : glm::vec2(1.0f, end));
        }
        if (auto *handle = uicontrols::Find<RectTransformComponent>(context, m_handle))
        {
            const float position = reversed ? 1.0f - t : t;
            handle->SetAnchorMin(horizontal ? glm::vec2(position, 0.0f) : glm::vec2(0.0f, position));
            handle->SetAnchorMax(horizontal ? glm::vec2(position, 1.0f) : glm::vec2(1.0f, position));
        }
    }

    std::vector<Property> UISliderComponent::Serialize() const
    {
        std::vector<Property> properties{
            BoolProperty("Enabled", IsEnabled()),
            FloatProperty("Minimum", m_minimum),
            FloatProperty("Maximum", m_maximum),
            FloatProperty("Value", m_value),
            BoolProperty("WholeNumbers", m_wholeNumbers),
            EnumProperty("Direction", m_direction, {"Left To Right", "Right To Left", "Bottom To Top", "Top To Bottom"}),
            EntityProperty("Fill", m_fill),
            EntityProperty("Handle", m_handle),
        };
        AppendSelectableProperties(properties);
        return properties;
    }

    void UISliderComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            const auto &name = property.name;
            if (name == "Enabled") SetEnabled(ReadBool(property.value));
            else if (name == "Minimum") m_minimum = ReadFloat(property.value, m_minimum);
            else if (name == "Maximum") m_maximum = ReadFloat(property.value, m_maximum);
            else if (name == "Value") m_value = ReadFloat(property.value, m_value);
            else if (name == "WholeNumbers") m_wholeNumbers = ReadBool(property.value);
            else if (name == "Direction") m_direction = ReadEnum(property.value, UISliderDirection::TopToBottom, m_direction);
            else if (name == "Fill") m_fill = ReadEntity(property.value);
            else if (name == "Handle") m_handle = ReadEntity(property.value);
            else ReadSelectableProperty(property);
        }
        SetValue(m_value); // properties may arrive in any order
    }
}
