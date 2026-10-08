#pragma once
#include <array>
#include <charconv>
#include <cmath>
#include <string_view>

namespace PlutoGE::assets
{
    inline constexpr unsigned kAffineSceneProjectVersion = 4;
    // Accepts either a header line or a complete scene, with LF or CRLF.
    inline unsigned SceneFormatVersion(std::string_view text)
    {
        if (text.starts_with("\xef\xbb\xbf")) text.remove_prefix(3);
        auto header = text.substr(0, text.find('\n'));
        if (header.ends_with('\r')) header.remove_suffix(1);
        return header == "SCENE\t1" ? 1 : header == "SCENE\t2" ? 2 : 0;
    }

    inline bool ParseSceneLinearCorrection(std::string_view text, std::array<float, 9> &values)
    {
        std::array<float, 9> parsed{};
        for (unsigned index = 0; index < parsed.size(); ++index)
        {
            const auto comma = text.find(',');
            if ((index < 8) != (comma != std::string_view::npos)) return false;
            const auto token = text.substr(0, comma);
            if (token.empty()) return false;
            const auto result = std::from_chars(token.data(), token.data() + token.size(), parsed[index]);
            if (result.ec != std::errc{} || result.ptr != token.data() + token.size() || !std::isfinite(parsed[index])) return false;
            if (comma != std::string_view::npos) text.remove_prefix(comma + 1);
        }
        values = parsed;
        return true;
    }
}
