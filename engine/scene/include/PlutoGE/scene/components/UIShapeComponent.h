#pragma once

#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/scene/ui/UIGraphic.h"
#include "PlutoGE/ui/render/Painter.h"

#include <vector>

namespace PlutoGE::scene
{
    // A resolution-independent box: solid or gradient fill, per-corner radii,
    // a border and an optional drop or inset shadow. Its radii also round
    // child clipping when the element clips its children.
    class UIShapeComponent : public TypedComponent<UIShapeComponent>, public IUIGraphic
    {
    public:
        void Update(float) override {}
        bool RequiresFrameUpdate() const override { return false; }
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        [[nodiscard]] UIPaintLayer GetPaintLayer() const override { return UIPaintLayer::Background; }
        void Paint(UIPaintContext &context) const override;

        [[nodiscard]] const ui::Paint &GetFill() const { return m_fill; }
        void SetFill(const ui::Paint &value) { m_fill = value; }
        [[nodiscard]] glm::vec4 GetColor() const { return m_fill.color; }
        void SetColor(glm::vec4 value) { m_fill.color = value; if (m_fill.type == ui::Paint::Type::Solid) m_fill.endColor = value; }
        [[nodiscard]] ui::CornerRadii GetCornerRadii() const { return m_radii; }
        void SetCornerRadii(const ui::CornerRadii &value);
        [[nodiscard]] float GetBorderWidth() const { return m_borderWidth; }
        void SetBorderWidth(float value) { m_borderWidth = std::max(value, 0.0f); }
        [[nodiscard]] glm::vec4 GetBorderColor() const { return m_borderColor; }
        void SetBorderColor(glm::vec4 value) { m_borderColor = value; }
        [[nodiscard]] bool HasShadow() const { return m_shadowEnabled; }
        void SetShadowEnabled(bool value) { m_shadowEnabled = value; }
        [[nodiscard]] const ui::ShadowStyle &GetShadow() const { return m_shadow; }
        void SetShadow(const ui::ShadowStyle &value) { m_shadow = value; }

    private:
        ui::Paint m_fill = ui::Paint::Solid(glm::vec4(1.0f));
        ui::CornerRadii m_radii;
        float m_borderWidth = 0.0f;
        glm::vec4 m_borderColor{0.0f, 0.0f, 0.0f, 1.0f};
        bool m_shadowEnabled = false;
        ui::ShadowStyle m_shadow;
    };
}
