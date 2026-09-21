#pragma once

#include "PlutoGE/render/BasicRenderer.h"
#include <algorithm>
#include <cmath>

namespace PlutoGE::render
{
    // Conservative sample footprint of BasicLit.slang's parallel-slab glass.
    // Unknown/deformed bounds and boxes crossing the eye plane use a full copy.
    inline rhi::Scissor GlassSnapshotBounds(const BasicDraw &draw, const glm::mat4 &viewProjection,
                                            glm::vec2 clipOffset, std::uint32_t width, std::uint32_t height, bool flipY)
    {
        const rhi::Scissor full{0, 0, width, height};
        if (draw.shadowBoundsRadius < 0 || !std::isfinite(draw.shadowBoundsRadius) || !std::isfinite(draw.thickness) ||
            draw.outlinePass || (draw.shaderGraphProgram && draw.shaderGraphProgram->data.header.z != 0))
            return full;
        glm::vec3 center = draw.shadowBoundsCenter;
        glm::vec3 extents(draw.shadowBoundsRadius);
        if (glm::all(glm::greaterThanEqual(draw.occlusionBoundsExtents, glm::vec3(0))))
        {
            center = draw.occlusionBoundsCenter;
            extents = draw.occlusionBoundsExtents;
        }
        // Both shader displacement terms have denominators clamped to 0.1;
        // refracted and view directions have length <= 1. This deliberately
        // covers all normals, normal maps, IORs and transmission values.
        extents += glm::vec3(20.0f * std::max(draw.thickness, 0.0f));
        glm::vec2 low(1), high(0);
        for (unsigned corner = 0; corner < 8; ++corner)
        {
            const glm::vec3 sign(corner & 1 ? 1 : -1, corner & 2 ? 1 : -1, corner & 4 ? 1 : -1);
            const glm::vec4 clip = viewProjection * glm::vec4(center + sign * extents, 1);
            if (!std::isfinite(clip.w) || clip.w <= 0.0001f)
                return full;
            glm::vec2 uv = (glm::vec2(clip) / clip.w + clipOffset) * .5f + .5f;
            if (flipY)
                uv.y = 1 - uv.y;
            if (!std::isfinite(uv.x) || !std::isfinite(uv.y))
                return full;
            low = glm::min(low, uv);
            high = glm::max(high, uv);
        }
        // The shader's largest blur tap is 12 pixels. Add two more for
        // bilinear filtering and conservative rounding at snapshot boundaries.
        const glm::vec2 dimensions(width, height);
        low = glm::clamp(glm::floor(low * dimensions) - 14.0f, glm::vec2(0), dimensions);
        high = glm::clamp(glm::ceil(high * dimensions) + 14.0f, glm::vec2(0), dimensions);
        return {static_cast<std::int32_t>(low.x), static_cast<std::int32_t>(low.y),
                static_cast<std::uint32_t>(high.x - low.x), static_cast<std::uint32_t>(high.y - low.y)};
    }

    struct GlassSnapshotGroup
    {
        std::size_t end;
        rhi::Scissor bounds;
    };

    inline bool GlassFootprintsOverlap(const rhi::Scissor &a, const rhi::Scissor &b)
    {
        return a.x < b.x + static_cast<std::int32_t>(b.width) &&
            b.x < a.x + static_cast<std::int32_t>(a.width) &&
            a.y < b.y + static_cast<std::int32_t>(b.height) &&
            b.y < a.y + static_cast<std::int32_t>(a.height);
    }

    // A sample footprint contains the pane's raster footprint too. Compare
    // actual footprints, not their enclosing rectangle (which includes gaps).
    // Bound group size to keep planning cost bounded for very large imports.
    inline GlassSnapshotGroup PlanGlassSnapshotGroup(std::span<const BasicDraw> draws,
        std::span<const rhi::Scissor> footprints, std::size_t first)
    {
        GlassSnapshotGroup group{first + 1, footprints[first]};
        const auto limit = std::min(draws.size(), first + std::size_t{64});
        while (group.end < limit && draws[group.end].surfaceType == 1u)
        {
            const auto &next = footprints[group.end];
            bool overlaps = false;
            for (auto index = first; index < group.end; ++index)
                if (GlassFootprintsOverlap(footprints[index], next))
                {
                    overlaps = true;
                    break;
                }
            if (overlaps) break;
            const auto right = group.bounds.x + static_cast<std::int32_t>(group.bounds.width);
            const auto bottom = group.bounds.y + static_cast<std::int32_t>(group.bounds.height);
            const auto nextRight = next.x + static_cast<std::int32_t>(next.width);
            const auto nextBottom = next.y + static_cast<std::int32_t>(next.height);
            const auto x = std::min(group.bounds.x, next.x), y = std::min(group.bounds.y, next.y);
            group.bounds = {x, y, static_cast<std::uint32_t>(std::max(right, nextRight) - x),
                static_cast<std::uint32_t>(std::max(bottom, nextBottom) - y)};
            ++group.end;
        }
        return group;
    }

    inline GlassSnapshotGroup PlanGlassSnapshotGroup(std::span<const BasicDraw> draws, std::size_t first,
        const glm::mat4 &viewProjection, glm::vec2 clipOffset, std::uint32_t width, std::uint32_t height, bool flipY)
    {
        std::vector<rhi::Scissor> footprints(draws.size());
        for (auto index = first; index < std::min(draws.size(), first + std::size_t{64}); ++index)
            footprints[index] = GlassSnapshotBounds(draws[index], viewProjection, clipOffset, width, height, flipY);
        return PlanGlassSnapshotGroup(draws, footprints, first);
    }
} // namespace PlutoGE::render
