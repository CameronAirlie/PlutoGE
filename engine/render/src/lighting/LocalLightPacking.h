#pragma once

#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/LightClusterConfig.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace PlutoGE::render
{
    // How many of each local light type a frame packs into the surface shaders' light arrays, in order: points, then
    // spots, then rectangles in the slots left (SceneLightingParameters.slang). A packed light's index is its shader
    // light id; lights beyond the counts are not shaded.
    struct PackedLocalLightCounts
    {
        std::size_t points = 0, spots = 0, rects = 0;
        [[nodiscard]] std::size_t Total() const noexcept { return points + spots + rects; }
    };

    inline constexpr std::size_t MaxPackedPointLights = 16, MaxPackedSpotLights = 16;

    [[nodiscard]] inline PackedLocalLightCounts PackLocalLightCounts(const BasicLighting &lighting) noexcept
    {
        const auto points = std::min(lighting.pointLights.size(), MaxPackedPointLights);
        const auto spots = std::min(lighting.spotLights.size(), MaxPackedSpotLights);
        return {points, spots, std::min<std::size_t>(lighting.rectLights.size(), PLUTO_MAX_LOCAL_LIGHTS - points - spots)};
    }

    // The type of a packed local light.
    enum class PackedLocalLightType : std::uint8_t { Point, Spot, Rect };

    // Calls `visit(light, id, type, index)` for every packed local light, in shader id order; `index` is the light's
    // position in its own list (pointLights, spotLights or rectLights).
    template <class Visit>
    void ForEachPackedLocalLight(const BasicLighting &lighting, Visit &&visit)
    {
        const auto counts = PackLocalLightCounts(lighting);
        std::uint32_t id = 0;
        for (std::size_t i = 0; i < counts.points; ++i)
            visit(lighting.pointLights[i], id++, PackedLocalLightType::Point, i);
        for (std::size_t i = 0; i < counts.spots; ++i)
            visit(lighting.spotLights[i].light, id++, PackedLocalLightType::Spot, i);
        for (std::size_t i = 0; i < counts.rects; ++i)
            visit(lighting.rectLights[i].light, id++, PackedLocalLightType::Rect, i);
    }
}
