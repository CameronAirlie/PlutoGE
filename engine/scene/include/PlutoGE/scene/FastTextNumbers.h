#pragma once

#include <charconv>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace PlutoGE::scene
{
    // Number text for heavy scene data. Floats use the shortest form that reads back as exactly the same value, and
    // neither direction depends on the locale, unlike stream formatting, which costs about ten times as much here.
    inline void AppendNumber(std::string &output, float value)
    {
        char buffer[32];
        const auto end = std::to_chars(buffer, buffer + sizeof(buffer), value).ptr;
        output.append(buffer, end);
    }

    inline void AppendNumber(std::string &output, std::uint64_t value)
    {
        char buffer[24];
        const auto end = std::to_chars(buffer, buffer + sizeof(buffer), value).ptr;
        output.append(buffer, end);
    }

    // Parses all of `token`. Empty results mean malformed text; also out-of-range values, as std::stof treated them.
    template <typename T>
    [[nodiscard]] std::optional<T> ParseNumberToken(std::string_view token)
    {
        T value{};
        const auto *begin = token.data();
        const auto *end = begin + token.size();
        if (begin != end && *begin == '+') ++begin; // from_chars rejects this; older writers and hand edits may have it.
        const auto [next, error] = std::from_chars(begin, end, value);
        if (error != std::errc{} || next != end) return std::nullopt;
        return value;
    }

    // The next delimiter-separated token, advancing `text` past it. Returns false once nothing is left.
    inline bool NextToken(std::string_view &text, char delimiter, std::string_view &token)
    {
        if (text.data() == nullptr) return false;
        const auto split = text.find(delimiter);
        if (split == std::string_view::npos)
        {
            token = text;
            text = {};
            return true;
        }
        token = text.substr(0, split);
        text.remove_prefix(split + 1);
        return true;
    }
}
