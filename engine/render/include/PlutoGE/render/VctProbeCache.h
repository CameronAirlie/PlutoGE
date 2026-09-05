#pragma once
#include <algorithm>
#include <cstdint>
#include <glm/glm.hpp>
namespace PlutoGE::render
{
    struct alignas(16) VctProbeParameters
    {
        glm::vec4 originSize{0.0f};
        glm::uvec4 counts{0u};
        glm::uvec4 update{0u};
    };
    static_assert(sizeof(VctProbeParameters) == 48);
    struct VctProbeSchedule
    {
        static constexpr std::uint32_t count = 4096;
        std::uint32_t cursor = 0, remaining = 0, initialized = 0;
        float blend = 0.0f;
        bool clear = true;
        void Reset() { *this = {}; }
        void Refresh() { remaining = count * 8; }
        std::uint32_t Budget(int requested) const
        { return std::min(remaining, static_cast<std::uint32_t>(std::clamp(requested, 1, 256))); }
        void Advance(std::uint32_t budget)
        {
            remaining -= budget; cursor = (cursor + budget) % count;
            initialized = std::min(count, initialized + budget);
            if (initialized == count) blend = std::min(1.0f, blend + 0.02f);
        }
    };
}
