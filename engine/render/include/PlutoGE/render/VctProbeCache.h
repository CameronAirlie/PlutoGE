#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
namespace PlutoGE::render
{
    inline float VctUpdateSpeed(float speed)
    { return std::isfinite(speed) ? std::clamp(speed, 0.125f, 16.0f) : 1.0f; }
    inline float VctHistoryWeight(float base, float speed)
    { return std::pow(std::clamp(base, 0.0f, 0.98f), VctUpdateSpeed(speed)); }
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
        std::array<bool, count> dirty{};
        std::uint32_t priorityCursor = 0;
        void Prioritize(glm::uvec3 low, glm::uvec3 high)
        {
            high = glm::min(high, glm::uvec3(16));
            for (auto z = low.z; z < high.z; ++z)
                for (auto y = low.y; y < high.y; ++y)
                    for (auto x = low.x; x < high.x; ++x)
                        dirty[x + 16 * y + 256 * z] = true;
        }
        template<class Dispatch>
        std::uint32_t UpdatePriority(std::uint32_t budget, Dispatch dispatch)
        {
            std::uint32_t used = 0, scanned = 0, runs = 0;
            while (used < budget && scanned < count && runs < 4)
            {
                const auto first = priorityCursor;
                std::uint32_t run = 0;
                while (first + run < count && dirty[first + run] && used + run < budget && scanned + run < count)
                {
                    dirty[first + run] = false;
                    ++run;
                }
                if (run) { dispatch(first, run); used += run; ++runs; }
                const auto advance = std::max(1u, run);
                scanned += advance; priorityCursor = (first + advance) % count;
            }
            return used;
        }
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
