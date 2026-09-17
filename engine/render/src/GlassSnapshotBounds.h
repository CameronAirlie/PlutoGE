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
} // namespace PlutoGE::render
