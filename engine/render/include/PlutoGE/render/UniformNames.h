#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace PlutoGE::render
{
    template <std::size_t Count>
    [[nodiscard]] std::array<std::string, Count> MakeNumberedUniformNames(std::string_view prefix)
    {
        std::array<std::string, Count> names;
        for (std::size_t index = 0; index < Count; ++index)
            names[index] = std::string(prefix) + std::to_string(index);
        return names;
    }

    template <std::size_t Count>
    [[nodiscard]] std::array<std::string, Count> MakeArrayUniformNames(std::string_view prefix)
    {
        std::array<std::string, Count> names;
        for (std::size_t index = 0; index < Count; ++index)
            names[index] = std::string(prefix) + "[" + std::to_string(index) + "]";
        return names;
    }
}
