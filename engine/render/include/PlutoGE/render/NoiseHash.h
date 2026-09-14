#pragma once
#include <cstdint>

namespace PlutoGE::render
{
    // Matches the shader lattice hash. Inputs are integer lattice coordinates.
    inline float NoiseHash(float x, float y)
    {
        std::uint32_t h = static_cast<std::uint32_t>(static_cast<std::int32_t>(x)) * 0x8da6b343u ^
                          static_cast<std::uint32_t>(static_cast<std::int32_t>(y)) * 0xd8163841u;
        h ^= h >> 16; h *= 0x7feb352du;
        h ^= h >> 15; h *= 0x846ca68bu;
        h ^= h >> 16;
        return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);
    }
}
