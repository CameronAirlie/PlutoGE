#pragma once

#include <string>
#include <string_view>

namespace PlutoGE::ui
{
    // Decodes UTF-8, replacing malformed sequences with U+FFFD.
    [[nodiscard]] std::u32string DecodeUtf8(std::string_view text);
    [[nodiscard]] std::string EncodeUtf8(std::u32string_view text);
    void AppendUtf8(std::string &output, char32_t codepoint);
}
