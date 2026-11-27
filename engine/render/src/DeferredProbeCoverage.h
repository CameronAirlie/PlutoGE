#pragma once

#include "DecalRasterBounds.h"
#include "PlutoGE/render/BasicRenderer.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace PlutoGE::render
{
    // Where the geometry pass can defer pixels to DeferredProbeIrradiance (BasicLit.slang gridDeferred): those of draws
    // the blended pass does not shade and that lightmaps do not light. Disjoint rectangles, empty when no draw can
    // defer a pixel; one covering the target when a draw's extent is unknown (no bounds, or wind that moves geometry
    // past them). Disjoint because the composite adds by blending: an overlap would add the light twice.
    struct DeferredProbeRegion
    {
        std::vector<rhi::Scissor> rectangles;
        [[nodiscard]] bool Empty() const noexcept { return rectangles.empty(); }
        [[nodiscard]] std::uint64_t PixelCount() const noexcept
        {
            std::uint64_t pixels = 0;
            for (const auto &rectangle : rectangles)
                pixels += std::uint64_t(rectangle.width) * rectangle.height;
            return pixels;
        }
    };

    // Each bounding sphere (per instance for instanced draws) is boxed and rasterized like a decal's box, onto a grid
    // of kDeferredProbeTile-pixel tiles; covered tiles merge into rectangles, row runs first, then equal runs down.
    inline constexpr std::uint32_t kDeferredProbeTile = 32;

    inline DeferredProbeRegion DeferredProbePixelRegion(std::span<const BasicDraw> draws, const glm::mat4 &viewProjection,
                                                        std::uint32_t width, std::uint32_t height, bool flipY,
                                                        bool lightmapsActive)
    {
        // Past this many instances a draw is taken to cover the target rather than projected one by one.
        constexpr std::size_t kMaxProjectedInstances = 256;
        const DeferredProbeRegion whole{{rhi::Scissor{0, 0, width, height}}};
        if (!width || !height)
            return {};
        const std::uint32_t columns = (width + kDeferredProbeTile - 1) / kDeferredProbeTile;
        const std::uint32_t rows = (height + kDeferredProbeTile - 1) / kDeferredProbeTile;
        std::vector<std::uint8_t> tiles(std::size_t(columns) * rows, 0);
        std::size_t coveredTiles = 0;
        const auto add = [&](const glm::vec3 &center, float radius)
        {
            const glm::mat4 box = glm::scale(glm::translate(glm::mat4(1.0f), center), glm::vec3(2.0f * radius));
            const auto bounds = DecalRasterBounds(viewProjection * box, width, height, flipY);
            if (!bounds.width || !bounds.height)
                return;
            const std::uint32_t x0 = std::uint32_t(bounds.x) / kDeferredProbeTile;
            const std::uint32_t y0 = std::uint32_t(bounds.y) / kDeferredProbeTile;
            const std::uint32_t x1 = std::min((std::uint32_t(bounds.x) + bounds.width - 1) / kDeferredProbeTile, columns - 1);
            const std::uint32_t y1 = std::min((std::uint32_t(bounds.y) + bounds.height - 1) / kDeferredProbeTile, rows - 1);
            for (std::uint32_t y = y0; y <= y1; ++y)
                for (std::uint32_t x = x0; x <= x1; ++x)
                {
                    auto &tile = tiles[std::size_t(y) * columns + x];
                    coveredTiles += tile ? 0 : 1;
                    tile = 1;
                }
        };
        for (const auto &draw : draws)
        {
            if (!draw.mesh || draw.alphaMode == 2 || draw.surfaceType == 1 || draw.outlinePass)
                continue;
            if (lightmapsActive && draw.lightmap.IsValid())
                continue;
            if (draw.foliageWind.enabled || draw.detailGrass)
                return whole;
            if (draw.instanceModels && !draw.instanceModels->empty())
            {
                const auto &local = draw.localBoundingSphere;
                if (local.w < 0.0f || draw.instanceModels->size() > kMaxProjectedInstances)
                    return whole;
                for (const auto &model : *draw.instanceModels)
                {
                    const float scale = std::max({glm::length(glm::vec3(model[0])), glm::length(glm::vec3(model[1])),
                                                  glm::length(glm::vec3(model[2]))});
                    add(glm::vec3(model * glm::vec4(glm::vec3(local), 1.0f)), local.w * scale);
                }
            }
            else if (draw.shadowBoundsRadius >= 0.0f)
                add(draw.shadowBoundsCenter, draw.shadowBoundsRadius);
            else
                return whole;
            if (coveredTiles == tiles.size())
                return whole;
        }

        // Row runs; a run continues the rectangle above it when it spans the same columns.
        DeferredProbeRegion region;
        struct Open { std::uint32_t x0, x1; std::size_t rectangle; };
        std::vector<Open> above, current;
        const auto pixel = [](std::uint32_t tile, std::uint32_t limit)
        {
            return std::min(tile * kDeferredProbeTile, limit);
        };
        for (std::uint32_t y = 0; y < rows; ++y)
        {
            current.clear();
            for (std::uint32_t x = 0; x < columns;)
            {
                if (!tiles[std::size_t(y) * columns + x]) { ++x; continue; }
                const std::uint32_t start = x;
                while (x < columns && tiles[std::size_t(y) * columns + x]) ++x;
                const auto match = std::find_if(above.begin(), above.end(),
                                                [&](const Open &open) { return open.x0 == start && open.x1 == x; });
                if (match != above.end())
                {
                    region.rectangles[match->rectangle].height = pixel(y + 1, height) - std::uint32_t(region.rectangles[match->rectangle].y);
                    current.push_back(*match);
                }
                else
                {
                    region.rectangles.push_back({std::int32_t(pixel(start, width)), std::int32_t(pixel(y, height)),
                                                 pixel(x, width) - pixel(start, width), pixel(y + 1, height) - pixel(y, height)});
                    current.push_back({start, x, region.rectangles.size() - 1});
                }
            }
            above.swap(current);
        }
        return region;
    }
}
