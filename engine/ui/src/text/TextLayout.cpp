#include "PlutoGE/ui/text/TextLayout.h"

#include "PlutoGE/ui/text/FontLibrary.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace PlutoGE::ui
{
    namespace
    {
        struct Item
        {
            ShapedGlyph shaped;
            std::uint32_t style = 0;
            char32_t codepoint = 0;
        };

        struct LineRange
        {
            std::size_t begin = 0;
            std::size_t end = 0;      // exclusive; excludes the newline
            bool forcedBreak = false; // ended by '\n' (justify skips it)
        };

        bool IsSpace(char32_t c) { return c == U' ' || c == U'\t' || c == 0x3000 || c == 0x200B; }

        bool IsCjk(char32_t c)
        {
            return (c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xAC00 && c <= 0xD7AF) || (c >= 0xF900 && c <= 0xFAFF) ||
                   (c >= 0xFF00 && c <= 0xFFEF) || (c >= 0x20000 && c <= 0x2FA1F);
        }

        // True when a line may end after `current` and before `next`.
        bool CanBreakAfter(char32_t current, char32_t next)
        {
            return IsSpace(current) || current == U'-' || IsCjk(current) || IsCjk(next);
        }

        const FontFace *ResolveFont(const TextStyle &style, FontLibrary &fonts)
        {
            return style.font ? style.font : fonts.GetDefaultFont();
        }

        float MeasureRange(const std::vector<Item> &items, std::size_t begin, std::size_t end, bool trimTrailing)
        {
            if (trimTrailing)
                while (end > begin && IsSpace(items[end - 1].codepoint))
                    --end;
            float width = 0.0f;
            for (std::size_t index = begin; index < end; ++index)
                width += items[index].shaped.advance;
            return width;
        }

        std::vector<LineRange> BreakLines(const std::vector<Item> &items, const TextLayoutSettings &settings)
        {
            std::vector<LineRange> lines;
            const bool wrap = settings.wrap != TextWrap::None && std::isfinite(settings.maxWidth);
            std::size_t lineStart = 0;
            std::size_t lastBreak = SIZE_MAX; // break allowed after this item
            float width = 0.0f;
            for (std::size_t index = 0; index < items.size();)
            {
                const char32_t codepoint = items[index].codepoint;
                if (codepoint == U'\n')
                {
                    lines.push_back({lineStart, index, true});
                    lineStart = index + 1;
                    lastBreak = SIZE_MAX;
                    width = 0.0f;
                    ++index;
                    continue;
                }
                const float advance = items[index].shaped.advance;
                // Whitespace may hang past the edge; it is trimmed from the line width.
                if (wrap && index > lineStart && !IsSpace(codepoint) && width + advance > settings.maxWidth)
                {
                    const bool wordBreak = settings.wrap == TextWrap::Word && lastBreak != SIZE_MAX && lastBreak >= lineStart;
                    std::size_t breakAt = wordBreak ? lastBreak + 1 : index;
                    lines.push_back({lineStart, breakAt, false});
                    while (breakAt < items.size() && IsSpace(items[breakAt].codepoint))
                        ++breakAt; // leading spaces of a wrapped line belong to the break
                    lineStart = breakAt;
                    lastBreak = SIZE_MAX;
                    width = 0.0f;
                    index = breakAt;
                    continue;
                }
                width += advance;
                const char32_t next = index + 1 < items.size() ? items[index + 1].codepoint : 0;
                if (CanBreakAfter(codepoint, next))
                    lastBreak = index;
                ++index;
            }
            lines.push_back({lineStart, items.size(), false});
            return lines;
        }

        void Shape(const TextContent &content, const std::u32string_view text, std::uint32_t clusterBase,
                   std::uint32_t styleIndex, FontLibrary &fonts, ITextShaper &shaper, std::vector<Item> &items)
        {
            const TextStyle &style = content.styles[styleIndex];
            const FontFace *font = ResolveFont(style, fonts);
            if (!font || text.empty())
                return;
            std::vector<ShapedGlyph> shaped;
            shaper.Shape({.text = text,
                          .clusterBase = clusterBase,
                          .font = font,
                          .fallbacks = fonts.GetFallbackFonts(),
                          .pixelSize = style.size,
                          .letterSpacing = style.letterSpacing},
                         shaped);
            for (const auto &glyph : shaped)
            {
                const std::uint32_t local = glyph.cluster - clusterBase;
                items.push_back({glyph, styleIndex, local < text.size() ? text[local] : U' '});
            }
        }

        // Shortens items[range] so that, with an ellipsis appended, it fits maxWidth.
        void Ellipsize(std::vector<Item> &items, LineRange &range, float maxWidth, const TextContent &content,
                       FontLibrary &fonts, ITextShaper &shaper)
        {
            const std::uint32_t style = range.end > range.begin ? items[range.end - 1].style
                                        : !items.empty()         ? items[std::min(range.begin, items.size() - 1)].style
                                                                 : 0;
            std::vector<Item> ellipsis;
            // Spelled numerically: MSVC without /utf-8 mis-encodes \u escapes in U"" literals.
            constexpr char32_t kEllipsis[] = {0x2026};
            const std::u32string_view mark(kEllipsis, 1);
            Shape(content, mark, 0, style, fonts, shaper, ellipsis);
            if (ellipsis.empty() || ellipsis.front().shaped.glyph == 0)
            {
                ellipsis.clear();
                Shape(content, U"...", 0, style, fonts, shaper, ellipsis);
            }
            float ellipsisWidth = 0.0f;
            for (const auto &item : ellipsis)
                ellipsisWidth += item.shaped.advance;

            std::size_t end = range.end;
            while (end > range.begin && MeasureRange(items, range.begin, end, true) + ellipsisWidth > maxWidth)
                --end;
            while (end > range.begin && IsSpace(items[end - 1].codepoint))
                --end;
            // Ellipsis clusters point just past the kept text so hit testing stays monotonic.
            const std::uint32_t cluster = end > range.begin ? items[end - 1].shaped.cluster + 1 : 0;
            for (auto &item : ellipsis)
                item.shaped.cluster = cluster;
            items.erase(items.begin() + static_cast<std::ptrdiff_t>(end), items.begin() + static_cast<std::ptrdiff_t>(range.end));
            items.insert(items.begin() + static_cast<std::ptrdiff_t>(end), ellipsis.begin(), ellipsis.end());
            range.end = end + ellipsis.size();
        }

        void AddDecorations(TextLayout &layout, const LaidOutLine &line, const std::vector<Item> &items,
                            std::size_t itemBegin, const TextContent &content, FontLibrary &fonts)
        {
            const auto emit = [&](std::size_t first, std::size_t last, bool strike)
            {
                const auto &firstGlyph = layout.glyphs[line.glyphBegin + (first - itemBegin)];
                const auto &lastGlyph = layout.glyphs[line.glyphBegin + (last - itemBegin)];
                const TextStyle &style = content.styles[firstGlyph.style];
                const FontFace *font = ResolveFont(style, fonts);
                const FontMetrics metrics = font ? font->GetMetrics() : FontMetrics{};
                const float thickness = std::max(metrics.underlineThickness * style.size, 1.0f);
                const float centre = strike ? line.baseline + metrics.ascender * style.size * 0.3f
                                            : line.baseline + metrics.underlinePosition * style.size;
                const float right = lastGlyph.pen.x + items[last].shaped.advance;
                layout.decorations.push_back({.rect = {{firstGlyph.pen.x, centre - thickness * 0.5f},
                                                       {right, centre + thickness * 0.5f}},
                                              .style = firstGlyph.style});
            };
            for (const bool strike : {false, true})
            {
                std::size_t runStart = SIZE_MAX;
                const std::size_t itemEnd = itemBegin + (line.glyphEnd - line.glyphBegin);
                for (std::size_t index = itemBegin; index <= itemEnd; ++index)
                {
                    bool decorated = false;
                    if (index < itemEnd)
                    {
                        const TextStyle &style = content.styles[items[index].style];
                        decorated = strike ? style.strikethrough : style.underline;
                        if (runStart != SIZE_MAX && items[index].style != items[runStart].style)
                        {
                            emit(runStart, index - 1, strike);
                            runStart = SIZE_MAX;
                        }
                    }
                    if (decorated && runStart == SIZE_MAX)
                        runStart = index;
                    else if (!decorated && runStart != SIZE_MAX)
                    {
                        emit(runStart, index - 1, strike);
                        runStart = SIZE_MAX;
                    }
                }
            }
        }
    }

    TextLayout LayoutText(const TextContent &content, const TextLayoutSettings &settings,
                          FontLibrary &fonts, ITextShaper &shaper)
    {
        TextLayout layout;
        if (content.styles.empty())
            return layout;

        // 1. Shape every span.
        std::vector<Item> items;
        items.reserve(content.text.size());
        const std::u32string_view text = content.text;
        if (content.spans.empty())
            Shape(content, text, 0, 0, fonts, shaper, items);
        else
            for (const auto &span : content.spans)
            {
                const std::uint32_t begin = std::min<std::uint32_t>(span.begin, static_cast<std::uint32_t>(text.size()));
                const std::uint32_t end = std::clamp<std::uint32_t>(span.end, begin, static_cast<std::uint32_t>(text.size()));
                if (span.style < content.styles.size())
                    Shape(content, text.substr(begin, end - begin), begin, span.style, fonts, shaper, items);
            }

        // 2. Break into lines and compute line metrics.
        std::vector<LineRange> ranges = BreakLines(items, settings);
        struct Metrics
        {
            float ascent = 0.0f;
            float height = 0.0f;
        };
        const auto measureLine = [&](const LineRange &range)
        {
            float ascent = 0.0f, descent = 0.0f, gap = 0.0f;
            const auto include = [&](std::uint32_t styleIndex)
            {
                const TextStyle &style = content.styles[styleIndex];
                const FontFace *font = ResolveFont(style, fonts);
                const FontMetrics metrics = font ? font->GetMetrics() : FontMetrics{};
                ascent = std::max(ascent, metrics.ascender * style.size);
                descent = std::min(descent, metrics.descender * style.size);
                gap = std::max(gap, metrics.lineGap * style.size);
            };
            for (std::size_t index = range.begin; index < range.end; ++index)
                include(items[index].style);
            if (range.begin == range.end)
                include(range.begin < items.size() ? items[range.begin].style
                        : items.empty()             ? 0
                                                    : items.back().style);
            return Metrics{ascent, (ascent - descent + gap) * std::max(settings.lineSpacing, 0.0f)};
        };

        // 3. Truncate to the available height and width.
        if (settings.overflow == TextOverflow::Ellipsis)
        {
            if (std::isfinite(settings.maxHeight) && ranges.size() > 1)
            {
                float height = 0.0f;
                std::size_t kept = 0;
                for (; kept < ranges.size(); ++kept)
                {
                    height += measureLine(ranges[kept]).height;
                    if (height > settings.maxHeight + 0.01f)
                        break;
                }
                kept = std::max<std::size_t>(kept, 1);
                if (kept < ranges.size())
                {
                    ranges.resize(kept);
                    LineRange &last = ranges.back();
                    last.end = std::max(last.end, last.begin);
                    // Force an ellipsis even if the last kept line fits.
                    const float maxWidth = std::isfinite(settings.maxWidth)
                                               ? std::min(settings.maxWidth, MeasureRange(items, last.begin, last.end, true))
                                               : MeasureRange(items, last.begin, last.end, true);
                    Ellipsize(items, last, maxWidth, content, fonts, shaper);
                    items.resize(last.end);
                    layout.truncated = true;
                }
            }
            if (std::isfinite(settings.maxWidth))
            {
                std::ptrdiff_t shift = 0;
                for (auto &range : ranges)
                {
                    range.begin += shift;
                    range.end += shift;
                    if (MeasureRange(items, range.begin, range.end, true) <= settings.maxWidth + 0.01f)
                        continue;
                    const std::size_t before = range.end;
                    Ellipsize(items, range, settings.maxWidth, content, fonts, shaper);
                    shift += static_cast<std::ptrdiff_t>(range.end) - static_cast<std::ptrdiff_t>(before);
                    layout.truncated = true;
                }
            }
        }

        // 4. Position glyphs.
        float contentWidth = 0.0f;
        for (const auto &range : ranges)
            contentWidth = std::max(contentWidth, MeasureRange(items, range.begin, range.end, true));
        const float alignWidth = std::isfinite(settings.maxWidth) ? settings.maxWidth : contentWidth;

        float cursorY = 0.0f;
        for (std::size_t lineIndex = 0; lineIndex < ranges.size(); ++lineIndex)
        {
            const LineRange &range = ranges[lineIndex];
            const Metrics metrics = measureLine(range);
            LaidOutLine line;
            line.top = cursorY;
            line.height = metrics.height;
            line.baseline = cursorY - metrics.ascent;
            line.width = MeasureRange(items, range.begin, range.end, true);

            float extraPerSpace = 0.0f;
            float offset = 0.0f;
            const float slack = alignWidth - line.width;
            switch (settings.horizontal)
            {
            case TextHorizontalAlign::Center: offset = slack * 0.5f; break;
            case TextHorizontalAlign::Right: offset = slack; break;
            case TextHorizontalAlign::Justify:
                if (!range.forcedBreak && lineIndex + 1 < ranges.size() && slack > 0.0f)
                {
                    std::size_t spaces = 0, end = range.end;
                    while (end > range.begin && IsSpace(items[end - 1].codepoint))
                        --end;
                    for (std::size_t index = range.begin; index < end; ++index)
                        spaces += IsSpace(items[index].codepoint) ? 1 : 0;
                    if (spaces > 0)
                    {
                        extraPerSpace = slack / static_cast<float>(spaces);
                        line.width = alignWidth;
                    }
                }
                break;
            default: break;
            }
            line.left = offset;

            line.glyphBegin = static_cast<std::uint32_t>(layout.glyphs.size());
            float x = offset;
            for (std::size_t index = range.begin; index < range.end; ++index)
            {
                const Item &item = items[index];
                const float advance = item.shaped.advance + (IsSpace(item.codepoint) ? extraPerSpace : 0.0f);
                layout.glyphs.push_back({.face = item.shaped.face,
                                         .glyph = item.shaped.glyph,
                                         .pen = glm::vec2(x, line.baseline) + item.shaped.offset,
                                         .advance = advance,
                                         .style = item.style,
                                         .cluster = item.shaped.cluster});
                x += advance;
            }
            line.glyphEnd = static_cast<std::uint32_t>(layout.glyphs.size());
            layout.lines.push_back(line);
            AddDecorations(layout, layout.lines.back(), items, range.begin, content, fonts);
            cursorY -= metrics.height;
        }
        layout.size = {contentWidth, -cursorY};

        // 5. Vertical alignment inside a bounded box.
        if (std::isfinite(settings.maxHeight) && settings.vertical != TextVerticalAlign::Top)
        {
            const float slack = settings.maxHeight - layout.size.y;
            const float shift = settings.vertical == TextVerticalAlign::Middle ? slack * 0.5f : slack;
            for (auto &glyph : layout.glyphs) glyph.pen.y -= shift;
            for (auto &line : layout.lines)
            {
                line.top -= shift;
                line.baseline -= shift;
            }
            for (auto &decoration : layout.decorations) decoration.rect = decoration.rect.Translated({0.0f, -shift});
        }
        return layout;
    }
}
