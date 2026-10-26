#pragma once

#include "PlutoGE/ui/Geometry.h"
#include "PlutoGE/ui/text/TextShaper.h"

#include <limits>
#include <string>
#include <vector>

namespace PlutoGE::ui
{
    class FontLibrary;

    enum class TextWrap : std::uint8_t { None, Word, Character };
    enum class TextOverflow : std::uint8_t { Overflow, Clip, Ellipsis };
    enum class TextHorizontalAlign : std::uint8_t { Left, Center, Right, Justify };
    enum class TextVerticalAlign : std::uint8_t { Top, Middle, Bottom };

    struct TextStyle
    {
        const FontFace *font = nullptr; // null uses the library default
        float size = 18.0f;              // pixels per em
        glm::vec4 color{1.0f};
        bool bold = false;
        bool italic = false;
        bool underline = false;
        bool strikethrough = false;
        float letterSpacing = 0.0f;

        friend bool operator==(const TextStyle &, const TextStyle &) = default;
    };

    // A range of codepoints [begin, end) drawn with styles[style].
    struct TextSpan
    {
        std::uint32_t begin = 0;
        std::uint32_t end = 0;
        std::uint32_t style = 0;

        friend bool operator==(const TextSpan &, const TextSpan &) = default;
    };

    struct TextContent
    {
        std::u32string text;
        std::vector<TextStyle> styles; // at least one
        std::vector<TextSpan> spans;   // empty means the whole text uses styles[0]

        friend bool operator==(const TextContent &, const TextContent &) = default;
    };

    struct TextLayoutSettings
    {
        float maxWidth = std::numeric_limits<float>::infinity();
        float maxHeight = std::numeric_limits<float>::infinity();
        TextWrap wrap = TextWrap::Word;
        TextOverflow overflow = TextOverflow::Overflow;
        TextHorizontalAlign horizontal = TextHorizontalAlign::Left;
        TextVerticalAlign vertical = TextVerticalAlign::Top;
        float lineSpacing = 1.0f; // multiplier of the font's line height

        friend bool operator==(const TextLayoutSettings &, const TextLayoutSettings &) = default;
    };

    // Positions are relative to the layout box's top-left corner, y-up, so
    // lines extend into negative y.
    struct LaidOutGlyph
    {
        const FontFace *face = nullptr;
        GlyphIndex glyph = 0;
        glm::vec2 pen{0.0f}; // baseline origin
        float advance = 0.0f; // horizontal space the glyph occupies, including justification
        std::uint32_t style = 0;
        std::uint32_t cluster = 0;
    };

    struct LaidOutLine
    {
        float left = 0.0f;
        float top = 0.0f;      // y of the line's top edge (<= 0)
        float width = 0.0f;    // excluding trailing whitespace
        float height = 0.0f;
        float baseline = 0.0f; // y of the baseline
        std::uint32_t glyphBegin = 0;
        std::uint32_t glyphEnd = 0;
    };

    struct TextDecoration
    {
        Rect rect;
        std::uint32_t style = 0;
    };

    struct TextLayout
    {
        std::vector<LaidOutGlyph> glyphs;
        std::vector<LaidOutLine> lines;
        std::vector<TextDecoration> decorations;
        glm::vec2 size{0.0f}; // natural content size
        bool truncated = false;
    };

    TextLayout LayoutText(const TextContent &content, const TextLayoutSettings &settings,
                          FontLibrary &fonts, ITextShaper &shaper);
}
