#include "PlutoGE/scene/components/UIDropdownComponent.h"

#include "PlutoGE/scene/components/UIComponent.h"
#include "PlutoGE/ui/render/Painter.h"
#include "PlutoGE/ui/text/FontLibrary.h"
#include "PlutoGE/ui/text/RichText.h"
#include "../ui/UIControlHelpers.h"
#include "../ui/UIPropertyCodec.h"

#include <cmath>

namespace PlutoGE::scene
{
    using namespace uiprops;

    namespace
    {
        constexpr float kPopupPadding = 4.0f;
        constexpr float kTextInset = 10.0f;

        std::vector<std::string> SplitLines(std::string_view text)
        {
            std::vector<std::string> lines;
            while (!text.empty())
            {
                const auto end = text.find('\n');
                lines.emplace_back(text.substr(0, end));
                if (end == std::string_view::npos)
                    break;
                text.remove_prefix(end + 1);
            }
            return lines;
        }

        std::string JoinLines(const std::vector<std::string> &lines)
        {
            std::string text;
            for (std::size_t index = 0; index < lines.size(); ++index)
                text += (index ? "\n" : "") + lines[index];
            return text;
        }
    }

    void UIDropdownComponent::SetOptions(std::vector<std::string> options)
    {
        m_options = std::move(options);
        SetValue(m_value);
    }

    void UIDropdownComponent::SetValue(int value)
    {
        m_value = m_options.empty() ? -1 : std::clamp(value, 0, static_cast<int>(m_options.size()) - 1);
    }

    int UIDropdownComponent::VisibleCount() const
    {
        return std::min(static_cast<int>(m_options.size()), std::max(m_maxVisibleItems, 1));
    }

    void UIDropdownComponent::ScrollTo(int index)
    {
        const int visible = VisibleCount();
        if (index < m_firstVisible)
            m_firstVisible = index;
        else if (index >= m_firstVisible + visible)
            m_firstVisible = index - visible + 1;
        m_firstVisible = std::clamp(m_firstVisible, 0, std::max(static_cast<int>(m_options.size()) - visible, 0));
    }

    ui::Rect UIDropdownComponent::GetPopupRect(const ui::Rect &ownerRect) const
    {
        const float height = static_cast<float>(VisibleCount()) * m_itemHeight + kPopupPadding * 2.0f;
        return {{ownerRect.min.x, ownerRect.min.y - height - 2.0f}, {ownerRect.max.x, ownerRect.min.y - 2.0f}};
    }

    int UIDropdownComponent::ItemAt(const ui::Rect &popup, glm::vec2 local) const
    {
        if (!popup.Contains(local))
            return -1;
        const int row = static_cast<int>(std::floor((popup.max.y - kPopupPadding - local.y) / m_itemHeight));
        const int index = m_firstVisible + row;
        return row >= 0 && row < VisibleCount() && index < static_cast<int>(m_options.size()) ? index : -1;
    }

    void UIDropdownComponent::Choose(int index, UIEventContext &context)
    {
        m_open = false;
        if (index < 0 || index == m_value)
            return;
        SetValue(index);
        context.Emit(UIEventType::ValueChanged, uicontrols::OwnerId(*this));
    }

    void UIDropdownComponent::OnUIEvent(UIEvent &event, UIEventContext &context)
    {
        if (!IsInteractable())
            return;
        switch (event.type)
        {
        case UIEventType::PointerDown:
            event.Consume();
            break;
        case UIEventType::Click:
        case UIEventType::Submit:
            if (!m_open && !m_options.empty())
            {
                m_open = true;
                m_hovered = std::max(m_value, 0);
                ScrollTo(m_hovered);
                context.OpenPopup(uicontrols::OwnerId(*this));
            }
            event.Consume();
            break;
        default:
            break;
        }
    }

    void UIDropdownComponent::OnPopupEvent(UIEvent &event, UIEventContext &context, const ui::Rect &ownerRect, glm::vec2 local)
    {
        const ui::Rect popup = GetPopupRect(ownerRect);
        const int count = static_cast<int>(m_options.size());
        switch (event.type)
        {
        case UIEventType::PointerMove:
            if (const int index = ItemAt(popup, local); index >= 0)
                m_hovered = index;
            break;
        case UIEventType::Click:
            if (const int index = ItemAt(popup, local); index >= 0)
                Choose(index, context);
            break;
        case UIEventType::Scroll:
            m_firstVisible = std::clamp(m_firstVisible - static_cast<int>(std::round(event.scroll.y)), 0,
                                        std::max(count - VisibleCount(), 0));
            break;
        case UIEventType::Move:
            if (event.direction.y != 0 && count > 0)
            {
                m_hovered = std::clamp(m_hovered - event.direction.y, 0, count - 1);
                ScrollTo(m_hovered);
            }
            break;
        case UIEventType::Submit:
            Choose(m_hovered, context);
            break;
        case UIEventType::Cancel:
            m_open = false;
            break;
        default:
            break;
        }
        event.Consume();
    }

    void UIDropdownComponent::PaintPopup(UIPaintContext &context) const
    {
        const ui::Rect popup = GetPopupRect(context.rect);
        const ui::CornerRadii radii = ui::CornerRadii::All(m_cornerRadius);
        ui::DrawShadow(context.list, popup, radii, {.color = {0, 0, 0, 0.45f}, .offset = {0, -4}, .blur = 14});
        ui::DrawBox(context.list, popup, {.fill = ui::Paint::Solid(m_popupColor), .radii = radii});

        auto &fonts = context.fonts;
        const ui::TextStyle style{.size = m_fontSize, .color = m_textColor};
        for (int row = 0; row < VisibleCount(); ++row)
        {
            const int index = m_firstVisible + row;
            if (index >= static_cast<int>(m_options.size()))
                break;
            const float top = popup.max.y - kPopupPadding - static_cast<float>(row) * m_itemHeight;
            const ui::Rect item{{popup.min.x + kPopupPadding, top - m_itemHeight}, {popup.max.x - kPopupPadding, top}};
            if (index == m_hovered)
                ui::DrawBox(context.list, item, {.fill = ui::Paint::Solid(m_highlightColor), .radii = ui::CornerRadii::All(4.0f)});
            if (index == m_value)
                ui::DrawBox(context.list, {{item.min.x + 3.0f, item.min.y + 8.0f}, {item.min.x + 6.0f, item.max.y - 8.0f}},
                            {.fill = ui::Paint::Solid(m_textColor), .radii = ui::CornerRadii::All(1.5f)});
            const ui::TextContent content = ui::ParseRichText(m_options[index], style, false);
            const ui::TextLayout layout = ui::LayoutText(content,
                                                         {.maxWidth = item.Width() - kTextInset * 2.0f,
                                                          .maxHeight = item.Height(),
                                                          .wrap = ui::TextWrap::None,
                                                          .overflow = ui::TextOverflow::Ellipsis,
                                                          .vertical = ui::TextVerticalAlign::Middle},
                                                         fonts, ui::GetTextShaper());
            ui::DrawText(context.list, layout, content, {item.min.x + kTextInset, item.max.y}, {}, fonts.GetAtlas());
        }
    }

    void UIDropdownComponent::DriveRects(UIEventContext &context)
    {
        auto *caption = m_caption != 0 ? uicontrols::Find<UITextComponent>(context, m_caption)
                        : GetOwner()        ? GetOwner()->GetComponent<UITextComponent>()
                                            : nullptr;
        const std::string text = m_value >= 0 && m_value < static_cast<int>(m_options.size()) ? m_options[m_value] : std::string{};
        if (caption && caption->GetText() != text)
            caption->SetText(text);
    }

    std::vector<Property> UIDropdownComponent::Serialize() const
    {
        std::vector<Property> properties{
            BoolProperty("Enabled", IsEnabled()),
            {"Options", PropertyType::String, JoinLines(m_options)},
            IntProperty("Value", m_value),
            EntityProperty("Caption", m_caption),
            FloatProperty("ItemHeight", m_itemHeight),
            IntProperty("MaxVisibleItems", m_maxVisibleItems),
            FloatProperty("FontSize", m_fontSize),
            FloatProperty("CornerRadius", m_cornerRadius),
            ColorProperty("PopupColor", m_popupColor),
            ColorProperty("HighlightColor", m_highlightColor),
            ColorProperty("TextColor", m_textColor),
        };
        AppendSelectableProperties(properties);
        return properties;
    }

    void UIDropdownComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            const auto &name = property.name;
            const auto &value = property.value;
            if (name == "Enabled") SetEnabled(ReadBool(value));
            else if (name == "Options") m_options = SplitLines(value);
            else if (name == "Value") m_value = ReadInt(value, m_value);
            else if (name == "Caption") m_caption = ReadEntity(value);
            else if (name == "ItemHeight") m_itemHeight = std::max(ReadFloat(value, m_itemHeight), 8.0f);
            else if (name == "MaxVisibleItems") m_maxVisibleItems = std::max(ReadInt(value, m_maxVisibleItems), 1);
            else if (name == "FontSize") m_fontSize = std::max(ReadFloat(value, m_fontSize), 4.0f);
            else if (name == "CornerRadius") m_cornerRadius = std::max(ReadFloat(value, m_cornerRadius), 0.0f);
            else if (name == "PopupColor") m_popupColor = ReadVector(value, m_popupColor);
            else if (name == "HighlightColor") m_highlightColor = ReadVector(value, m_highlightColor);
            else if (name == "TextColor") m_textColor = ReadVector(value, m_textColor);
            else ReadSelectableProperty(property);
        }
        SetValue(m_value);
    }
}
