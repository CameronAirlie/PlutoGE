#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace PlutoGE::render
{
    // Which triangles hardware ray-traced GI traces for a draw (RayTracingScene::TracedRange). Diffuse GI needs a
    // model's mass, silhouette and coverage, not its fine detail, and a dense canopy's thousands of alpha-tested leaf
    // triangles make every ray that crosses it test far more candidates than its light is worth.
    enum class RayTracedGeometryDetail : std::uint8_t
    {
        // The range the geometry pass draws (the frontend's whole-cluster LOD for instanced draws).
        AsDrawn,
        // Instanced draws with a LOD chain (foliage, scattered rocks) trace the chain's coarsest mesh LOD; other draws
        // as drawn.
        CoarsestLod,
    };

    inline constexpr std::array<std::string_view, 2> RayTracedGeometryDetailNames{"AsDrawn", "CoarsestLod"};

    [[nodiscard]] constexpr std::string_view RayTracedGeometryDetailName(RayTracedGeometryDetail detail) noexcept
    {
        const auto index = static_cast<std::size_t>(detail);
        return index < RayTracedGeometryDetailNames.size() ? RayTracedGeometryDetailNames[index] : RayTracedGeometryDetailNames[0];
    }

    [[nodiscard]] constexpr std::optional<RayTracedGeometryDetail> ParseRayTracedGeometryDetail(std::string_view name) noexcept
    {
        for (std::size_t index = 0; index < RayTracedGeometryDetailNames.size(); ++index)
            if (RayTracedGeometryDetailNames[index] == name)
                return static_cast<RayTracedGeometryDetail>(index);
        return std::nullopt;
    }
}
