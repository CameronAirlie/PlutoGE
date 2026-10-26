#include "PlutoGE/ui/text/Utf8.h"

namespace PlutoGE::ui
{
    namespace
    {
        constexpr char32_t kReplacement = 0xFFFD;

        bool IsContinuation(unsigned char value) { return (value & 0xC0) == 0x80; }
    }

    std::u32string DecodeUtf8(std::string_view text)
    {
        std::u32string result;
        result.reserve(text.size());
        for (std::size_t index = 0; index < text.size();)
        {
            const auto lead = static_cast<unsigned char>(text[index]);
            std::size_t length = 0;
            char32_t codepoint = 0;
            if (lead < 0x80) { length = 1; codepoint = lead; }
            else if ((lead & 0xE0) == 0xC0) { length = 2; codepoint = lead & 0x1F; }
            else if ((lead & 0xF0) == 0xE0) { length = 3; codepoint = lead & 0x0F; }
            else if ((lead & 0xF8) == 0xF0) { length = 4; codepoint = lead & 0x07; }
            else
            {
                result.push_back(kReplacement);
                ++index;
                continue;
            }

            if (index + length > text.size())
            {
                result.push_back(kReplacement);
                break;
            }
            bool valid = true;
            for (std::size_t offset = 1; offset < length; ++offset)
            {
                const auto next = static_cast<unsigned char>(text[index + offset]);
                valid &= IsContinuation(next);
                codepoint = (codepoint << 6) | (next & 0x3F);
            }
            // Reject overlong encodings, surrogates and out-of-range values.
            constexpr char32_t minimum[] = {0, 0, 0x80, 0x800, 0x10000};
            if (!valid || codepoint < minimum[length] || codepoint > 0x10FFFF ||
                (codepoint >= 0xD800 && codepoint <= 0xDFFF))
            {
                result.push_back(kReplacement);
                index += valid ? length : 1;
                continue;
            }
            result.push_back(codepoint);
            index += length;
        }
        return result;
    }

    void AppendUtf8(std::string &output, char32_t codepoint)
    {
        if (codepoint < 0x80)
            output.push_back(static_cast<char>(codepoint));
        else if (codepoint < 0x800)
        {
            output.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
        else if (codepoint < 0x10000)
        {
            output.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
        else
        {
            output.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
    }

    std::string EncodeUtf8(std::u32string_view text)
    {
        std::string result;
        result.reserve(text.size());
        for (const char32_t codepoint : text)
            AppendUtf8(result, codepoint);
        return result;
    }
}
