#include "PlutoGE/scene/components/UIComponent.h"

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/UIShapeComponent.h"
#include "PlutoGE/ui/layout/Layout.h"
#include "PlutoGE/ui/render/Painter.h"
#include "PlutoGE/ui/text/FontLibrary.h"
#include "PlutoGE/ui/text/RichText.h"

#include <algorithm>
#include <cmath>

// Drawing and measuring for the RectTransform-based UI components. Property
// serialization lives in UIComponent.cpp.
namespace PlutoGE::scene
{
    namespace
    {
        // Legacy procedural shapes express thickness and radii as percentages
        // of the half size; keep that so existing scenes look the same.
        float Percent(float value) { return value * 0.01f; }

        glm::vec4 Tinted(glm::vec4 color, const UIPaintContext &context)
        {
            color *= context.tint;
            color.a *= context.opacity;
            return color;
        }

        ui::ImageDrawMode ToDrawMode(UIImageType type)
        {
            switch (type)
            {
            case UIImageType::Sliced: return ui::ImageDrawMode::Sliced;
            case UIImageType::Tiled: return ui::ImageDrawMode::Tiled;
            case UIImageType::FilledHorizontal: return ui::ImageDrawMode::FilledHorizontal;
            case UIImageType::FilledVertical: return ui::ImageDrawMode::FilledVertical;
            case UIImageType::FilledRadial: return ui::ImageDrawMode::FilledRadial;
            default: return ui::ImageDrawMode::Simple;
            }
        }

        void DrawCrosshair(ui::DrawList &list, const ui::Rect &rect, float thickness, float gap, const glm::vec4 &color)
        {
            const glm::vec2 half = rect.Size() * 0.5f, centre = rect.Center();
            const float width = std::max(thickness, 0.002f);
            const float clampedGap = std::clamp(gap, 0.0f, 0.95f);
            const ui::BoxStyle style{.fill = ui::Paint::Solid(color)};
            const float barHalf = width * half.y, gapX = clampedGap * half.x;
            ui::DrawBox(list, {{rect.min.x, centre.y - barHalf}, {centre.x - gapX, centre.y + barHalf}}, style);
            ui::DrawBox(list, {{centre.x + gapX, centre.y - barHalf}, {rect.max.x, centre.y + barHalf}}, style);
            const float barHalfX = width * half.x, gapY = clampedGap * half.y;
            ui::DrawBox(list, {{centre.x - barHalfX, rect.min.y}, {centre.x + barHalfX, centre.y - gapY}}, style);
            ui::DrawBox(list, {{centre.x - barHalfX, centre.y + gapY}, {centre.x + barHalfX, rect.max.y}}, style);
        }
    }

    void UIImageComponent::Paint(UIPaintContext &context) const
    {
        const glm::vec4 color = Tinted(m_color, context);
        if (color.a <= 0.0f)
            return;
        const ui::Rect &rect = context.rect;
        const float halfMin = std::min(rect.Width(), rect.Height()) * 0.5f;
        switch (m_imageType)
        {
        case UIImageType::ProceduralCircle:
            ui::DrawRing(context.list, rect, {.color = color, .thickness = Percent(m_thickness) * halfMin});
            return;
        case UIImageType::ProceduralArc:
            ui::DrawRing(context.list, rect, {.color = color,
                                              .thickness = Percent(m_thickness) * halfMin,
                                              .startDegrees = m_startAngle,
                                              .amount = m_fillAmount,
                                              .clockwise = m_fillClockwise});
            return;
        case UIImageType::ProceduralCrosshair:
            DrawCrosshair(context.list, rect, Percent(m_thickness), m_fillAmount, color);
            return;
        case UIImageType::ProceduralRoundedRect:
        {
            const float radius = std::clamp(Percent(m_cornerRadius), 0.0f, 1.0f) * halfMin;
            const bool outline = m_thickness > 0.0f;
            ui::DrawBox(context.list, rect,
                        {.fill = ui::Paint::Solid(outline ? glm::vec4(glm::vec3(color), 0.0f) : color),
                         .radii = ui::CornerRadii::All(radius),
                         .borderWidth = outline ? Percent(m_thickness) * halfMin : 0.0f,
                         .borderColor = color});
            return;
        }
        default:
            break;
        }

        const bool textured = !m_texturePath.empty();
        ui::DrawImage(context.list, rect,
                      {.texture = textured ? ui::TextureKey::Asset(m_texturePath) : ui::TextureKey::White(),
                       .textureSize = textured ? context.textures.GetTextureSize(m_texturePath) : glm::vec2(0.0f),
                       .tint = color,
                       .mode = ToDrawMode(m_imageType),
                       .sliceBorder = ui::Edges::FromVec4(m_border),
                       .sliceScale = m_sliceScale,
                       .fillAmount = m_fillAmount,
                       .fillClockwise = m_fillClockwise,
                       .fillStartDegrees = m_startAngle,
                       .preserveAspect = m_preserveAspect,
                       .radii = ui::CornerRadii::All(m_cornerRadius)});
    }

    glm::vec2 UIImageComponent::MeasureContent(glm::vec2, UIMeasureContext &context) const
    {
        return m_texturePath.empty() ? glm::vec2(0.0f) : context.textures.GetTextureSize(m_texturePath);
    }

    ui::TextStyle UITextComponent::BaseStyle(ui::FontLibrary &fonts) const
    {
        return {.font = m_fontPath.empty() ? nullptr : fonts.Load(m_fontPath),
                .size = std::max(m_fontSize, 1.0f),
                .color = m_color,
                .bold = m_bold,
                .italic = m_italic,
                .letterSpacing = m_letterSpacing};
    }

    ui::TextLayoutSettings UITextComponent::Settings(glm::vec2 box) const
    {
        const auto alignment = static_cast<int>(m_alignment);
        constexpr ui::TextHorizontalAlign kHorizontal[] = {
            ui::TextHorizontalAlign::Left, ui::TextHorizontalAlign::Center, ui::TextHorizontalAlign::Right};
        constexpr ui::TextVerticalAlign kVertical[] = {
            ui::TextVerticalAlign::Top, ui::TextVerticalAlign::Middle, ui::TextVerticalAlign::Bottom};
        constexpr ui::TextOverflow kOverflow[] = {
            ui::TextOverflow::Overflow, ui::TextOverflow::Clip, ui::TextOverflow::Ellipsis};
        return {.maxWidth = box.x,
                .maxHeight = box.y,
                .wrap = m_wrap ? ui::TextWrap::Word : ui::TextWrap::None,
                .overflow = kOverflow[static_cast<int>(m_overflow)],
                .horizontal = kHorizontal[alignment % 3],
                .vertical = kVertical[alignment / 3],
                .lineSpacing = m_lineSpacing};
    }

    const ui::TextContent &UITextComponent::GetContent(ui::FontLibrary &fonts) const
    {
        const ui::TextStyle style = BaseStyle(fonts);
        if (m_contentRevision == 0 || m_contentText != m_text || m_contentRich != m_richText || !(m_contentStyle == style))
        {
            m_content = ui::ParseRichText(m_text, style, m_richText,
                                          [&fonts](std::string_view reference) { return fonts.Load(std::string(reference)); });
            m_contentText = m_text;
            m_contentRich = m_richText;
            m_contentStyle = style;
            ++m_contentRevision;
        }
        return m_content;
    }

    const ui::TextLayout &UITextComponent::GetLayout(glm::vec2 box, ui::FontLibrary &fonts) const
    {
        GetContent(fonts);
        const ui::TextLayoutSettings settings = Settings(box);
        for (auto &slot : m_layoutCache)
            if (slot.valid && slot.contentRevision == m_contentRevision && slot.settings == settings)
                return slot.layout;
        auto &slot = m_layoutCache[m_nextCacheSlot];
        m_nextCacheSlot = (m_nextCacheSlot + 1) % std::size(m_layoutCache);
        slot.layout = ui::LayoutText(m_content, settings, fonts, ui::GetTextShaper());
        slot.settings = settings;
        slot.contentRevision = m_contentRevision;
        slot.valid = true;
        return slot.layout;
    }

    ui::Rect UITextComponent::GetTextRect(const ui::Rect &rect) const
    {
        return ui::Inset(rect, ui::Edges::FromVec4(m_padding));
    }

    glm::vec2 UITextComponent::MeasureContent(glm::vec2 available, UIMeasureContext &context) const
    {
        const glm::vec2 padding{m_padding.x + m_padding.z, m_padding.y + m_padding.w};
        if (m_text.empty())
            return padding;
        const float width = m_wrap && std::isfinite(available.x) ? std::max(available.x - padding.x, 0.0f) : ui::kUnbounded;
        return GetLayout({width, ui::kUnbounded}, context.fonts).size + padding;
    }

    void UITextComponent::Paint(UIPaintContext &context) const
    {
        if (m_text.empty() || context.opacity <= 0.0f)
            return;
        const ui::Rect rect = GetTextRect(context.rect);
        // A little slack so text measured at exactly this width does not
        // re-wrap from rounding in the layout pass.
        const ui::TextLayout &layout = GetLayout({rect.Width() + 0.5f, rect.Height()}, context.fonts);
        const bool clip = m_overflow == UITextOverflow::Clip || m_scrollOffset != 0.0f;
        if (clip)
            context.list.PushClip(rect);
        ui::DrawText(context.list, layout, m_content, {rect.min.x - m_scrollOffset, rect.max.y},
                     {.outlineColor = m_outlineColor,
                      .outlineWidth = m_outlineWidth,
                      .shadowColor = m_shadowColor,
                      .shadowOffset = m_shadowOffset,
                      .shadowSoftness = m_shadowSoftness,
                      .opacity = context.opacity},
                     context.fonts.GetAtlas());
        if (clip)
            context.list.PopClip();
    }

    void UIButtonComponent::Paint(UIPaintContext &context) const
    {
        const Entity *owner = GetOwner();
        if (!owner)
            return;
        const auto *image = owner->GetComponent<UIImageComponent>();
        const auto *shape = owner->GetComponent<UIShapeComponent>();
        if ((image && image->IsEnabled()) || (shape && shape->IsEnabled()))
            return;
        // Legacy default face for buttons without artwork.
        ui::DrawBox(context.list, context.rect,
                    {.fill = ui::Paint::Solid(Tinted(glm::vec4(0.16f, 0.18f, 0.22f, 0.92f), context))});
    }
}
