#pragma once

#include <cstdint>

namespace PlutoGE::render
{
    enum class ShadowMethod : std::uint8_t
    {
        Cascaded,
        Virtual
    };
}
