#include "PlutoGE/scene/components/UIInputFieldComponent.h"

#include "PlutoGE/scene/components/UIComponent.h"
#include "PlutoGE/ui/render/Painter.h"
#include "PlutoGE/ui/text/FontLibrary.h"
#include "PlutoGE/ui/text/Utf8.h"
#include "../ui/UIControlHelpers.h"
#include "../ui/UIPropertyCodec.h"

#include <cmath>

namespace PlutoGE::scene
{
    using namespace uiprops;

    namespace
    {
        constexpr char32_t kPasswordMask = 0x2022; // bullet

        bool IsSpace(char32_t c) { return c == U' ' || c == U'\t' || c == U'\n'; }
        bool IsDigit(char32_t c) { return c >= U'0' && c <= U'9'; }
        bool IsAsciiAlphanumeric(char32_t c)
        {
            return IsDigit(c) || (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z');
        }

        struct CaretPlacement
        {
            float x = 0.0f;
            float top = 0.0f;
            float height = 0.0f;
        };

        // Where the caret before codepoint `index` sits, relative to the
        // layout box's top-left corner.
        CaretPlacement PlaceCaret(const ui::TextLayout &layout, std::size_t index)
        {
            if (layout.lines.empty())
                return {};
            const ui::LaidOutGlyph *before = nullptr; // last glyph ending at or before the caret
            const ui::LaidOutLine *beforeLine = &layout.lines.front();
            for (const auto &line : layout.lines)
                for (std::uint32_t g = line.glyphBegin; g < line.glyphEnd; ++g)
                {
                    const auto &glyph = layout.glyphs[g];
                    if (glyph.cluster == index)
                        return {glyph.pen.x, line.top, line.height};
                    if (glyph.cluster < index && (!before || glyph.cluster >= before->cluster))
                    {
                        before = &glyph;
                        beforeLine = &line;
                    }
                }
            if (!before)
                return {layout.lines.front().left, layout.lines.front().top, layout.lines.front().height};
            return {before->pen.x + before->advance, beforeLine->top, beforeLine->height};
        }
    }

    UITextComponent *UIInputFieldComponent::TextComponent() const
    {
        return GetOwner() ? GetOwner()->GetComponent<UITextComponent>() : nullptr;
    }

    void UIInputFieldComponent::SetText(std::string value)
    {
        m_text = std::move(value);
        m_codepoints = ui::DecodeUtf8(m_text);
        m_caret = std::min(m_caret, m_codepoints.size());
        m_anchor = std::min(m_anchor, m_codepoints.size());
    }

    void UIInputFieldComponent::BeginEdit()
    {
        if (m_editing || !IsInteractable())
            return;
        m_editing = true;
        m_original = m_text;
        m_codepoints = ui::DecodeUtf8(m_text);
        m_caret = m_anchor = m_codepoints.size();
        m_blink = 0.0f;
    }

    void UIInputFieldComponent::EndEdit(UIEventContext &context)
    {
        if (!m_editing)
            return;
        m_editing = false;
        m_scroll = 0.0f;
        m_anchor = m_caret;
        context.Emit(UIEventType::EndEdit, uicontrols::OwnerId(*this));
    }

    bool UIInputFieldComponent::Accepts(char32_t codepoint, std::size_t position) const
    {
        if (codepoint == U'\n' || codepoint == U'\t')
        {
            if (!m_multiline)
                return false;
        }
        else if (codepoint < 0x20 || codepoint == 0x7F)
            return false;
        if (m_characterLimit > 0 && m_codepoints.size() >= static_cast<std::size_t>(m_characterLimit))
            return false;
        const bool hasMinus = m_codepoints.find(U'-') != std::u32string::npos;
        switch (m_contentType)
        {
        case UIInputContentType::Integer:
            return IsDigit(codepoint) || (codepoint == U'-' && position == 0 && !hasMinus);
        case UIInputContentType::Decimal:
            return IsDigit(codepoint) || (codepoint == U'-' && position == 0 && !hasMinus) ||
                   (codepoint == U'.' && m_codepoints.find(U'.') == std::u32string::npos);
        case UIInputContentType::Alphanumeric:
            return IsAsciiAlphanumeric(codepoint);
        default:
            return true;
        }
    }

    bool UIInputFieldComponent::DeleteSelection()
    {
        if (m_caret == m_anchor)
            return false;
        const std::size_t low = std::min(m_caret, m_anchor), high = std::max(m_caret, m_anchor);
        m_codepoints.erase(low, high - low);
        m_caret = m_anchor = low;
        return true;
    }

    void UIInputFieldComponent::Insert(std::u32string_view text)
    {
        DeleteSelection();
        for (const char32_t codepoint : text)
            if (Accepts(codepoint, m_caret))
                m_codepoints.insert(m_codepoints.begin() + static_cast<std::ptrdiff_t>(m_caret++), codepoint);
        m_anchor = m_caret;
    }

    std::size_t UIInputFieldComponent::WordBoundary(std::size_t from, int direction) const
    {
        std::size_t index = from;
        if (direction < 0)
        {
            while (index > 0 && IsSpace(m_codepoints[index - 1])) --index;
            while (index > 0 && !IsSpace(m_codepoints[index - 1])) --index;
        }
        else
        {
            while (index < m_codepoints.size() && !IsSpace(m_codepoints[index])) ++index;
            while (index < m_codepoints.size() && IsSpace(m_codepoints[index])) ++index;
        }
        return index;
    }

    void UIInputFieldComponent::Commit(UIEventContext &context)
    {
        m_text = ui::EncodeUtf8(m_codepoints);
        m_blink = 0.0f;
        context.Emit(UIEventType::ValueChanged, uicontrols::OwnerId(*this));
    }

    void UIInputFieldComponent::OnTextInput(std::u32string_view text, std::span<const UIKeyPress> keys, UIEventContext &context)
    {
        if (!m_editing)
            return;
        const std::u32string before = m_codepoints;
        const auto lineStart = [&](std::size_t from)
        {
            while (from > 0 && m_codepoints[from - 1] != U'\n') --from;
            return from;
        };
        const auto lineEnd = [&](std::size_t from)
        {
            while (from < m_codepoints.size() && m_codepoints[from] != U'\n') ++from;
            return from;
        };

        for (const auto &press : keys)
        {
            const std::size_t low = std::min(m_caret, m_anchor), high = std::max(m_caret, m_anchor);
            const auto moveTo = [&](std::size_t position)
            {
                m_caret = std::min(position, m_codepoints.size());
                if (!press.shift)
                    m_anchor = m_caret;
            };
            switch (press.key)
            {
            case UIKey::Backspace:
                if (m_readOnly || DeleteSelection() || m_caret == 0)
                    break;
                {
                    const std::size_t from = press.control ? WordBoundary(m_caret, -1) : m_caret - 1;
                    m_codepoints.erase(from, m_caret - from);
                    m_caret = m_anchor = from;
                }
                break;
            case UIKey::Delete:
                if (m_readOnly || DeleteSelection() || m_caret >= m_codepoints.size())
                    break;
                {
                    const std::size_t to = press.control ? WordBoundary(m_caret, 1) : m_caret + 1;
                    m_codepoints.erase(m_caret, to - m_caret);
                }
                break;
            case UIKey::Left:
                if (!press.shift && m_caret != m_anchor) moveTo(low);
                else moveTo(press.control ? WordBoundary(m_caret, -1) : (m_caret > 0 ? m_caret - 1 : 0));
                break;
            case UIKey::Right:
                if (!press.shift && m_caret != m_anchor) moveTo(high);
                else moveTo(press.control ? WordBoundary(m_caret, 1) : m_caret + 1);
                break;
            case UIKey::Home:
                moveTo(m_multiline && !press.control ? lineStart(m_caret) : 0);
                break;
            case UIKey::End:
                moveTo(m_multiline && !press.control ? lineEnd(m_caret) : m_codepoints.size());
                break;
            case UIKey::Up:
            case UIKey::Down:
                if (m_multiline)
                {
                    // Same column on the neighbouring line, clamped to its length.
                    const std::size_t start = lineStart(m_caret), column = m_caret - start;
                    if (press.key == UIKey::Up && start > 0)
                    {
                        const std::size_t previous = lineStart(start - 1);
                        moveTo(std::min(previous + column, start - 1));
                    }
                    else if (press.key == UIKey::Down)
                    {
                        const std::size_t end = lineEnd(m_caret);
                        if (end < m_codepoints.size())
                            moveTo(std::min(end + 1 + column, lineEnd(end + 1)));
                    }
                }
                break;
            case UIKey::Enter:
                if (m_multiline && !m_readOnly)
                    Insert(U"\n");
                break;
            case UIKey::SelectAll:
                m_anchor = 0;
                m_caret = m_codepoints.size();
                break;
            case UIKey::Copy:
            case UIKey::Cut:
                if (high > low && m_contentType != UIInputContentType::Password)
                {
                    context.SetClipboardText(ui::EncodeUtf8(std::u32string_view(m_codepoints).substr(low, high - low)));
                    if (press.key == UIKey::Cut && !m_readOnly)
                        DeleteSelection();
                }
                break;
            case UIKey::Paste:
                if (!m_readOnly)
                {
                    std::u32string pasted = ui::DecodeUtf8(context.GetClipboardText());
                    if (!m_multiline)
                        std::replace(pasted.begin(), pasted.end(), U'\n', U' ');
                    std::erase(pasted, U'\r');
                    Insert(pasted);
                }
                break;
            }
        }
        if (!text.empty() && !m_readOnly)
            Insert(text);
        m_blink = 0.0f;
        if (m_codepoints != before)
            Commit(context);
    }

    std::size_t UIInputFieldComponent::IndexAt(glm::vec2 local, const ui::Rect &rect) const
    {
        const auto *text = TextComponent();
        if (!text)
            return m_codepoints.size();
        const ui::Rect area = text->GetTextRect(rect);
        const ui::TextLayout &layout = text->GetLayout({area.Width() + 0.5f, area.Height()}, ui::FontLibrary::Get());
        const glm::vec2 point = local - glm::vec2(area.min.x - m_scroll, area.max.y);
        if (layout.lines.empty())
            return 0;
        // The line containing the point, or the nearest one.
        std::size_t lineIndex = layout.lines.size() - 1;
        for (std::size_t index = 0; index < layout.lines.size(); ++index)
            if (point.y >= layout.lines[index].top - layout.lines[index].height)
            {
                lineIndex = index;
                break;
            }
        const auto &line = layout.lines[lineIndex];
        for (std::uint32_t g = line.glyphBegin; g < line.glyphEnd; ++g)
            if (point.x < layout.glyphs[g].pen.x + layout.glyphs[g].advance * 0.5f)
                return layout.glyphs[g].cluster;
        // Past the end of the line: before its newline, or at the very end.
        if (line.glyphEnd > line.glyphBegin && lineIndex + 1 < layout.lines.size())
            return layout.glyphs[line.glyphEnd - 1].cluster + 1;
        return lineIndex + 1 < layout.lines.size() ? 0 : m_codepoints.size();
    }

    void UIInputFieldComponent::OnUIEvent(UIEvent &event, UIEventContext &context)
    {
        if (!IsInteractable())
            return;
        const UIEntityID owner = uicontrols::OwnerId(*this);
        switch (event.type)
        {
        case UIEventType::PointerDown:
        case UIEventType::Drag:
            if (const ui::Rect *rect = uicontrols::OwnerRect(context, *this))
            {
                const bool starting = event.type == UIEventType::PointerDown;
                if (starting)
                    BeginEdit();
                m_caret = IndexAt(context.ToLocal(owner, event.position), *rect);
                if (starting)
                    m_anchor = m_caret;
                m_blink = 0.0f;
            }
            event.Consume();
            break;
        case UIEventType::BeginDrag:
        case UIEventType::Click:
            event.Consume();
            break;
        case UIEventType::Select:
            BeginEdit();
            break;
        case UIEventType::Deselect:
            EndEdit(context);
            break;
        case UIEventType::Submit:
            if (m_editing && !m_multiline)
                EndEdit(context);
            else if (!m_editing)
                BeginEdit();
            event.Consume();
            break;
        case UIEventType::Cancel:
            if (m_editing)
            {
                if (m_text != m_original)
                {
                    m_codepoints = ui::DecodeUtf8(m_original);
                    Commit(context);
                }
                EndEdit(context);
                event.Consume();
            }
            break;
        default:
            break;
        }
    }

    bool UIInputFieldComponent::HandleMove(glm::ivec2 direction, UIEventContext &)
    {
        // While editing, arrows move the caret (delivered as keys); vertical
        // movement leaves a single-line field.
        return m_editing && (direction.x != 0 || m_multiline);
    }

    void UIInputFieldComponent::DriveRects(UIEventContext &context)
    {
        if (!m_editing)
            m_codepoints = ui::DecodeUtf8(m_text);
        if (auto *text = TextComponent())
        {
            text->SetRichText(false); // typed text is never markup
            text->SetWrap(m_multiline);
            const std::string display = m_contentType == UIInputContentType::Password
                                            ? ui::EncodeUtf8(std::u32string(m_codepoints.size(), kPasswordMask))
                                            : m_text;
            if (text->GetText() != display)
                text->SetText(display);
            text->SetScrollOffset(m_editing ? m_scroll : 0.0f);
        }
        if (auto *placeholder = uicontrols::Find<RectTransformComponent>(context, m_placeholder))
            placeholder->SetOpacity(m_codepoints.empty() ? 1.0f : 0.0f);
    }

    void UIInputFieldComponent::UpdateUI(UIEventContext &context, float deltaTime)
    {
        m_blink += deltaTime;
        const auto *text = TextComponent();
        const ui::Rect *rect = uicontrols::OwnerRect(context, *this);
        if (!m_editing || m_multiline || !text || !rect)
            return;
        // Scroll a single line so the caret stays inside the field.
        const ui::Rect area = text->GetTextRect(*rect);
        const ui::TextLayout &layout = text->GetLayout({area.Width() + 0.5f, area.Height()}, ui::FontLibrary::Get());
        const float caret = PlaceCaret(layout, m_caret).x;
        const float width = area.Width();
        if (caret - m_scroll > width - m_caretWidth)
            m_scroll = caret - width + m_caretWidth;
        if (caret - m_scroll < 0.0f)
            m_scroll = caret;
        m_scroll = std::max(m_scroll, 0.0f);
    }

    void UIInputFieldComponent::Paint(UIPaintContext &context) const
    {
        const auto *text = TextComponent();
        if (!m_editing || !text)
            return;
        const ui::Rect rect = text->GetTextRect(context.rect);
        const ui::TextLayout &layout = text->GetLayout({rect.Width() + 0.5f, rect.Height()}, context.fonts);
        const glm::vec2 origin{rect.min.x - m_scroll, rect.max.y};
        context.list.PushClip(rect);

        // Selection: one band per line over the selected glyphs.
        const std::size_t low = std::min(m_caret, m_anchor), high = std::max(m_caret, m_anchor);
        if (high > low)
        {
            glm::vec4 color = m_selectionColor;
            color.a *= context.opacity;
            for (const auto &line : layout.lines)
            {
                float left = 1e30f, right = -1e30f;
                for (std::uint32_t g = line.glyphBegin; g < line.glyphEnd; ++g)
                {
                    const auto &glyph = layout.glyphs[g];
                    if (glyph.cluster >= low && glyph.cluster < high)
                    {
                        left = std::min(left, glyph.pen.x);
                        right = std::max(right, glyph.pen.x + glyph.advance);
                    }
                }
                if (right > left)
                    ui::DrawBox(context.list, {origin + glm::vec2(left, line.top - line.height), origin + glm::vec2(right, line.top)},
                                {.fill = ui::Paint::Solid(color)});
            }
        }

        // Caret: visible for the first half of each blink cycle.
        const bool visible = m_caretBlinkRate <= 0.0f || std::fmod(m_blink, m_caretBlinkRate) < m_caretBlinkRate * 0.5f;
        if (visible)
        {
            const CaretPlacement caret = PlaceCaret(layout, m_caret);
            glm::vec4 color = m_caretColor;
            color.a *= context.opacity;
            const float x = origin.x + caret.x;
            ui::DrawBox(context.list,
                        {{x - m_caretWidth * 0.5f, origin.y + caret.top - caret.height}, {x + m_caretWidth * 0.5f, origin.y + caret.top}},
                        {.fill = ui::Paint::Solid(color)});
        }
        context.list.PopClip();
    }

    bool UIInputFieldComponent::GetRuntimeProperty(std::string_view name, std::string &value) const
    {
        if (name != "IsEditing")
            return false;
        value = FormatBool(m_editing);
        return true;
    }

    std::vector<Property> UIInputFieldComponent::Serialize() const
    {
        std::vector<Property> properties{
            BoolProperty("Enabled", IsEnabled()),
            {"Text", PropertyType::String, m_text},
            EntityProperty("Placeholder", m_placeholder),
            IntProperty("CharacterLimit", m_characterLimit),
            EnumProperty("ContentType", m_contentType, {"Standard", "Integer", "Decimal", "Alphanumeric", "Password"}),
            BoolProperty("Multiline", m_multiline),
            BoolProperty("ReadOnly", m_readOnly),
            ColorProperty("CaretColor", m_caretColor),
            ColorProperty("SelectionColor", m_selectionColor),
            FloatProperty("CaretBlinkRate", m_caretBlinkRate),
            FloatProperty("CaretWidth", m_caretWidth),
        };
        AppendSelectableProperties(properties);
        return properties;
    }

    void UIInputFieldComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            const auto &name = property.name;
            const auto &value = property.value;
            if (name == "Enabled") SetEnabled(ReadBool(value));
            else if (name == "Text") SetText(value);
            else if (name == "Placeholder") m_placeholder = ReadEntity(value);
            else if (name == "CharacterLimit") SetCharacterLimit(ReadInt(value, m_characterLimit));
            else if (name == "ContentType") m_contentType = ReadEnum(value, UIInputContentType::Password, m_contentType);
            else if (name == "Multiline") m_multiline = ReadBool(value);
            else if (name == "ReadOnly") m_readOnly = ReadBool(value);
            else if (name == "CaretColor") m_caretColor = ReadVector(value, m_caretColor);
            else if (name == "SelectionColor") m_selectionColor = ReadVector(value, m_selectionColor);
            else if (name == "CaretBlinkRate") m_caretBlinkRate = std::max(ReadFloat(value, m_caretBlinkRate), 0.0f);
            else if (name == "CaretWidth") m_caretWidth = std::max(ReadFloat(value, m_caretWidth), 0.5f);
            else ReadSelectableProperty(property);
        }
    }
}
