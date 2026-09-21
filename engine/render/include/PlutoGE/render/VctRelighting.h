#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <glm/glm.hpp>

namespace PlutoGE::render
{
    struct VctLocalLight
    {
        glm::vec4 positionRange{}, colorIntensity{}, directionSpot{}, cone{};
        bool operator==(const VctLocalLight &) const = default;
    };

    // Both the old and new influence must be updated when a light moves,
    // turns off, or changes slots. Bounds are conservative for spot cones.
    struct alignas(16) VctRelightRegion
    {
        glm::uvec4 origin{}, extent{};
        std::uint64_t VoxelCount() const { return std::uint64_t(extent.x) * extent.y * extent.z; }
    };
    static_assert(sizeof(VctRelightRegion) == 32);

    inline VctRelightRegion VctChangedLightRegion(std::span<const VctLocalLight> previous,
        std::span<const VctLocalLight> current, glm::vec3 volumeOrigin, float volumeSize,
        std::uint32_t resolution, bool fullRefresh)
    {
        if (fullRefresh) return {{0,0,0,0}, {resolution,resolution,resolution,0}};
        glm::vec3 low(volumeOrigin + volumeSize), high(volumeOrigin);
        const auto include = [&](const VctLocalLight &light) {
            if (light.positionRange.w <= 0 || light.colorIntensity.w <= 0) return;
            low = glm::min(low, glm::vec3(light.positionRange) - light.positionRange.w);
            high = glm::max(high, glm::vec3(light.positionRange) + light.positionRange.w);
        };
        for (std::size_t i = 0; i < std::max(previous.size(), current.size()); ++i)
        {
            if (i < previous.size() && i < current.size() && previous[i] == current[i]) continue;
            if (i < previous.size()) include(previous[i]);
            if (i < current.size()) include(current[i]);
        }
        const glm::vec3 scale(float(resolution) / volumeSize);
        const auto start = glm::ivec3(glm::clamp(glm::floor((low - volumeOrigin) * scale), glm::vec3(0), glm::vec3(resolution)));
        const auto end = glm::ivec3(glm::clamp(glm::ceil((high - volumeOrigin) * scale), glm::vec3(0), glm::vec3(resolution)));
        return {glm::uvec4(start, 0), glm::uvec4(glm::max(end - start, glm::ivec3(0)), 0)};
    }
}
