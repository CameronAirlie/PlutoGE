#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
namespace PlutoGE::render
{
    inline float VctUpdateSpeed(float speed)
    { return std::isfinite(speed) ? std::clamp(speed, 0.125f, 16.0f) : 1.0f; }
    inline std::uint32_t VctUpdateBudget(std::uint32_t base, float speed)
    { return std::max(1u, static_cast<std::uint32_t>(std::ceil(base * VctUpdateSpeed(speed)))); }
    inline std::uint32_t VctUpdateInterval(std::uint32_t frames, float speed)
    { return std::max(1u, static_cast<std::uint32_t>(std::ceil(frames / VctUpdateSpeed(speed)))); }
    inline std::uint32_t VctBounceSlices(std::uint32_t resolution, float speed)
    {
        const auto baseGroups = std::max(1u, 32768u / (resolution * resolution) / 4u);
        return std::min(resolution, VctUpdateBudget(baseGroups, speed) * 4u);
    }

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
        std::uint32_t Budget(int requested, float speed = 1.0f) const
        { return std::min(remaining, std::min(count, VctUpdateBudget(static_cast<std::uint32_t>(std::clamp(requested, 1, 256)), speed))); }
        void Advance(std::uint32_t budget, float speed = 1.0f)
        {
            remaining -= budget; cursor = (cursor + budget) % count;
            initialized = std::min(count, initialized + budget);
            if (initialized == count) blend = std::min(1.0f, blend + 0.02f * VctUpdateSpeed(speed));
        }
    };
}
