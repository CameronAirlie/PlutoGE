#include "PlutoGE/ui/text/RichText.h"

#include "PlutoGE/ui/text/Utf8.h"

#include <algorithm>
#include <charconv>
#include <string>
#include <vector>

namespace PlutoGE::ui
{
    namespace
    {
        int HexDigit(char value)
        {
            if (value >= '0' && value <= '9') return value - '0';
            if (value >= 'a' && value <= 'f') return value - 'a' + 10;
            if (value >= 'A' && value <= 'F') return value - 'A' + 10;
            return -1;
        }

        bool ParseColor(std::string_view value, glm::vec4 &color)
        {
            if ((value.size() != 7 && value.size() != 9) || value[0] != '#')
                return false;
            int channels[4] = {0, 0, 0, 255};
            const int count = value.size() == 9 ? 4 : 3;
            for (int channel = 0; channel < count; ++channel)
            {
                const int high = HexDigit(value[1 + channel * 2]), low = HexDigit(value[2 + channel * 2]);
                if (high < 0 || low < 0)
                    return false;
                channels[channel] = high * 16 + low;
            }
            color = glm::vec4(channels[0], channels[1], channels[2], channels[3]) / 255.0f;
            return true;
        }

        std::string_view TagName(std::string_view tag)
        {
            return tag.substr(0, tag.find('='));
        }

        struct Frame
        {
            std::string_view name;
            TextStyle style;
        };

        // Applies an opening tag to `style`. Returns false for unknown or malformed tags.
        bool ApplyTag(std::string_view tag, TextStyle &style, const RichTextFontResolver &fonts)
        {
            const std::string_view name = TagName(tag);
            const std::string_view value = name.size() < tag.size() ? tag.substr(name.size() + 1) : std::string_view{};
            if (tag == "b") { style.bold = true; return true; }
            if (tag == "i") { style.italic = true; return true; }
            if (tag == "u") { style.underline = true; return true; }
            if (tag == "s") { style.strikethrough = true; return true; }
            if (name == "color")
                return ParseColor(value, style.color);
            if (name == "size")
            {
                float size = 0.0f;
                const auto result = std::from_chars(value.data(), value.data() + value.size(), size);
                if (result.ec != std::errc{} || result.ptr != value.data() + value.size())
                    return false;
                style.size = std::clamp(size, 1.0f, 512.0f);
                return true;
            }
            if (name == "font" && !value.empty())
            {
                if (fonts)
                    if (const FontFace *face = fonts(value))
                        style.font = face;
                return true;
            }
            return false;
        }
    }

    TextContent ParseRichText(std::string_view utf8, const TextStyle &base, bool markup, const RichTextFontResolver &fonts)
    {
        TextContent content;
        content.styles.push_back(base);
        if (!markup)
        {
            content.text = DecodeUtf8(utf8);
            return content;
        }

        std::vector<Frame> stack{{{}, base}};
        std::string literal;
        std::uint32_t currentStyle = 0;
        std::uint32_t spanStart = 0;

        const auto flushLiteral = [&]
        {
            if (literal.empty())
                return;
            content.text += DecodeUtf8(literal);
            literal.clear();
        };
        const auto closeSpan = [&]
        {
            flushLiteral();
            const auto end = static_cast<std::uint32_t>(content.text.size());
            if (end > spanStart)
                content.spans.push_back({spanStart, end, currentStyle});
            spanStart = end;
        };
        const auto activateStyle = [&](const TextStyle &style)
        {
            closeSpan();
            const auto found = std::find(content.styles.begin(), content.styles.end(), style);
            currentStyle = static_cast<std::uint32_t>(found - content.styles.begin());
            if (found == content.styles.end())
                content.styles.push_back(style);
        };

        for (std::size_t index = 0; index < utf8.size();)
        {
            if (utf8[index] == '<')
            {
                const auto close = utf8.find('>', index + 1);
                if (close != std::string_view::npos)
                {
                    const std::string_view tag = utf8.substr(index + 1, close - index - 1);
                    if (tag == "br" || tag == "br/" || tag == "br /")
                    {
                        literal.push_back('\n');
                        index = close + 1;
                        continue;
                    }
                    if (tag.size() > 1 && tag[0] == '/')
                    {
                        const std::string_view name = tag.substr(1);
                        const auto match = std::find_if(stack.rbegin(), stack.rend() - 1,
                                                        [&](const Frame &frame) { return frame.name == name; });
                        if (match != stack.rend() - 1)
                        {
                            stack.erase(std::next(match).base(), stack.end());
                            activateStyle(stack.back().style);
                            index = close + 1;
                            continue;
                        }
                    }
                    else
                    {
                        TextStyle style = stack.back().style;
                        if (ApplyTag(tag, style, fonts))
                        {
                            stack.push_back({TagName(tag), style});
                            activateStyle(style);
                            index = close + 1;
                            continue;
                        }
                    }
                }
            }
            literal.push_back(utf8[index]);
            ++index;
        }
        closeSpan();
        return content;
    }
}
