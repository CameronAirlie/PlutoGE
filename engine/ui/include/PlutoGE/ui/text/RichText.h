#pragma once

#include "PlutoGE/ui/text/TextLayout.h"

#include <functional>
#include <string_view>

namespace PlutoGE::ui
{
    // Resolves <font=...> references; returning null keeps the current font.
    using RichTextFontResolver = std::function<const FontFace *(std::string_view reference)>;

    // Converts UTF-8 text with optional markup into styled content.
    // Supported tags: <b> <i> <u> <s> <color=#RRGGBB[AA]> <size=N> <font=ref>
    // and <br>. Closing tags pop the most recent matching style; unknown or
    // malformed tags are kept as literal text.
    [[nodiscard]] TextContent ParseRichText(std::string_view utf8, const TextStyle &base, bool markup = true,
                                            const RichTextFontResolver &fonts = {});
}
