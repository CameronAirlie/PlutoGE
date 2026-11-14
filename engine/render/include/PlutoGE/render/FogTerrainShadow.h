#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace PlutoGE::render
{
    // Sun occlusion by terrain for volumetric fog, beyond the reach of shadow maps (a distant ridge in front of a
    // low sun). A square world-space grid over the terrains' XZ footprint stores, per texel, the world height below
    // which the terrain hides the sun: fog above it is sunlit, fog below it is in the terrain's shadow
    // (VolumetricFog.slang fogTerrainSunVisibility).
    struct FogTerrainShadow
    {
        glm::vec2 origin{0.0f};  // world XZ of texel (0, 0)'s corner
        float extent = 0.0f;     // world size of the square grid
        int resolution = 0;      // texels per side
        std::vector<float> shadowHeights;
        std::uint64_t revision = 0; // changes whenever the heights do

        [[nodiscard]] bool Valid() const noexcept
        {
            return resolution > 0 && extent > 0.0f &&
                   shadowHeights.size() == static_cast<std::size_t>(resolution) * static_cast<std::size_t>(resolution);
        }
    };

    // A height grid texel without terrain under it. Finite, so bilinear filtering stays finite.
    inline constexpr float kFogTerrainNoHeight = -1.0e6f;

    // The shadow heights of a `resolution` x `resolution` grid of terrain heights (row-major, rows along +Z, texel
    // size `cellSize`; kFogTerrainNoHeight where there is no terrain) for a sun in `directionToSun` (world, toward
    // the sun). One sweep from the sun's side carries the highest occluder toward the sun's far side, lowered by
    // the sun's slope over the distance walked. A sun at or below the horizon shadows nothing (the fog has no direct
    // sun then).
    [[nodiscard]] std::vector<float> ComputeFogTerrainShadowHeights(std::span<const float> heights, int resolution,
                                                                    float cellSize, const glm::vec3 &directionToSun);
}
