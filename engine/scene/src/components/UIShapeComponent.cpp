#include "PlutoGE/scene/components/UIShapeComponent.h"

#include "../ui/UIPropertyCodec.h"

namespace PlutoGE::scene
{
    using namespace uiprops;

    void UIShapeComponent::SetCornerRadii(const ui::CornerRadii &value)
    {
        m_radii = ui::CornerRadii::FromVec4(glm::max(value.ToVec4(), glm::vec4(0.0f)));
    }

    void UIShapeComponent::Paint(UIPaintContext &context) const
    {
        const auto tinted = [&](glm::vec4 color)
        {
            color *= context.tint;
            color.a *= context.opacity;
            return color;
        };
        if (m_shadowEnabled && !m_shadow.inset)
        {
            ui::ShadowStyle shadow = m_shadow;
            shadow.color.a *= context.opacity;
            ui::DrawShadow(context.list, context.rect, m_radii, shadow);
        }
        ui::Paint fill = m_fill;
        fill.color = tinted(fill.color);
        fill.endColor = tinted(fill.endColor);
        ui::DrawBox(context.list, context.rect,
                    {.fill = fill, .radii = m_radii, .borderWidth = m_borderWidth, .borderColor = tinted(m_borderColor)});
        if (m_shadowEnabled && m_shadow.inset)
        {
            ui::ShadowStyle shadow = m_shadow;
            shadow.color.a *= context.opacity;
            ui::DrawShadow(context.list, context.rect, m_radii, shadow);
        }
    }

    std::vector<Property> UIShapeComponent::Serialize() const
    {
        return {
            BoolProperty("Enabled", IsEnabled()),
            EnumProperty("FillType", m_fill.type, {"Solid", "Linear Gradient"}),
            ColorProperty("Color", m_fill.color),
            ColorProperty("EndColor", m_fill.endColor),
            FloatProperty("GradientAngle", m_fill.angleDegrees),
            Vec4Property("CornerRadii", m_radii.ToVec4()),
            FloatProperty("BorderWidth", m_borderWidth),
            ColorProperty("BorderColor", m_borderColor),
            BoolProperty("Shadow", m_shadowEnabled),
            ColorProperty("ShadowColor", m_shadow.color),
            Vec2Property("ShadowOffset", m_shadow.offset),
            FloatProperty("ShadowBlur", m_shadow.blur),
            FloatProperty("ShadowSpread", m_shadow.spread),
            BoolProperty("ShadowInset", m_shadow.inset),
        };
    }

    void UIShapeComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            const auto &name = property.name;
            const auto &value = property.value;
            if (name == "Enabled") SetEnabled(ReadBool(value));
            else if (name == "FillType") m_fill.type = ReadEnum(value, ui::Paint::Type::LinearGradient, m_fill.type);
            else if (name == "Color") m_fill.color = ReadVector(value, m_fill.color);
            else if (name == "EndColor") m_fill.endColor = ReadVector(value, m_fill.endColor);
            else if (name == "GradientAngle") m_fill.angleDegrees = ReadFloat(value, m_fill.angleDegrees);
            else if (name == "CornerRadii") SetCornerRadii(ui::CornerRadii::FromVec4(ReadVector(value, m_radii.ToVec4())));
            else if (name == "BorderWidth") SetBorderWidth(ReadFloat(value, m_borderWidth));
            else if (name == "BorderColor") m_borderColor = ReadVector(value, m_borderColor);
            else if (name == "Shadow") m_shadowEnabled = ReadBool(value);
            else if (name == "ShadowColor") m_shadow.color = ReadVector(value, m_shadow.color);
            else if (name == "ShadowOffset") m_shadow.offset = ReadVector(value, m_shadow.offset);
            else if (name == "ShadowBlur") m_shadow.blur = std::max(ReadFloat(value, m_shadow.blur), 0.0f);
            else if (name == "ShadowSpread") m_shadow.spread = ReadFloat(value, m_shadow.spread);
            else if (name == "ShadowInset") m_shadow.inset = ReadBool(value);
        }
    }
}
