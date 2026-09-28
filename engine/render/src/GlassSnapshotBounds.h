#pragma once

#include "PlutoGE/render/BasicRenderer.h"
#include <algorithm>
#include <cmath>

namespace PlutoGE::render
{
    enum class GlassBoundsReason { Bounded, Unknown, Deformed, NearPlane, NonFinite };
    enum class GlassGroupBoundary { End, RasterOverlap, SampleOverlap, Limit, OtherSurface };

    // Conservative sample footprint of BasicLit.slang's parallel-slab glass.
    // Unknown/deformed bounds use a full copy. Clip known boxes at the eye
    // plane, retaining the shader's positive-W denominator clamp interval.
    inline rhi::Scissor GlassSnapshotBounds(const BasicDraw &draw, const glm::mat4 &viewProjection,
                                            glm::vec2 clipOffset, std::uint32_t width, std::uint32_t height, bool flipY, bool sampleFootprint = true,
                                            GlassBoundsReason *reason = nullptr)
    {
        const rhi::Scissor full{0, 0, width, height};
        const auto fallback = [&](GlassBoundsReason why) { if (reason) *reason = why; return full; };
        if (reason) *reason = GlassBoundsReason::Bounded;
        if (draw.outlinePass || (draw.shaderGraphProgram && draw.shaderGraphProgram->data.header.z != 0))
            return fallback(GlassBoundsReason::Deformed);
        if (draw.shadowBoundsRadius < 0) return fallback(GlassBoundsReason::Unknown);
        if (!std::isfinite(draw.shadowBoundsRadius) || !std::isfinite(draw.thickness) || !std::isfinite(draw.ior))
            return fallback(GlassBoundsReason::NonFinite);
        glm::vec3 center = draw.shadowBoundsCenter;
        glm::vec3 extents(draw.shadowBoundsRadius);
        if (glm::all(glm::greaterThanEqual(draw.occlusionBoundsExtents, glm::vec3(0))))
        {
            center = draw.occlusionBoundsCenter;
            extents = draw.occlusionBoundsExtents;
        }
        // Snell's law bounds the refracted normal component from below by
        // sqrt(1 - 1/ior^2). Match the shader's IOR clamp and denominator floor;
        // the view-ray term still needs its conservative 10*thickness bound.
        const float ior = std::clamp(draw.ior, 1.0f, 3.0f);
        const float refractedCosine = std::sqrt(std::max(0.0f, 1.0f - 1.0f / (ior * ior)));
        const float displacement = 10.0f + 1.0f / std::max(0.1f, refractedCosine);
        if (sampleFootprint) extents += glm::vec3(displacement * std::max(draw.thickness, 0.0f));
        glm::vec2 low(1), high(0);
        std::array<glm::vec4, 8> corners;
        bool anyInFront = false;
        const auto include = [&](glm::vec4 clip)
        {
            glm::vec2 uv = ((glm::vec2(clip) + clipOffset * clip.w) / std::max(clip.w, 0.0001f)) * .5f + .5f;
            if (flipY) uv.y = 1 - uv.y;
            low = glm::min(low, uv);
            high = glm::max(high, uv);
            // Include the unclamped raster projection too: sceneUv remains a
            // possible sample when transmission blends or refraction is behind
            // the eye. At w=0 use the one-sided projective limit.
            if (clip.w > 0 && clip.w < .0001f)
            {
                uv = (glm::vec2(clip) / clip.w + clipOffset) * .5f + .5f;
                if (flipY) uv.y = 1 - uv.y;
                low = glm::min(low, uv); high = glm::max(high, uv);
            }
            else if (clip.w == 0)
            {
                const glm::vec2 direction(clip.x, flipY ? -clip.y : clip.y);
                for (int axis = 0; axis < 2; ++axis)
                {
                    if (direction[axis] < 0) low[axis] = 0;
                    if (direction[axis] > 0) high[axis] = 1;
                }
            }
        };
        for (unsigned corner = 0; corner < corners.size(); ++corner)
        {
            const glm::vec3 sign(corner & 1 ? 1 : -1, corner & 2 ? 1 : -1, corner & 4 ? 1 : -1);
            const glm::vec4 clip = viewProjection * glm::vec4(center + sign * extents, 1);
            if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.z) || !std::isfinite(clip.w))
                return fallback(GlassBoundsReason::NonFinite);
            corners[corner] = clip;
            if (clip.w > 0) anyInFront = true;
            if (clip.w >= 0) include(clip);
        }
        if (!anyInFront) return fallback(GlassBoundsReason::NearPlane);
        // Extrema of a projected convex box occur at its clipped vertices.
        // Refraction uses max(w, epsilon), so retain intersections at both
        // boundaries of that piecewise projection. No depth-plane clipping:
        // valid refracted samples can lie before the camera's near plane.
        for (unsigned corner = 0; corner < corners.size(); ++corner)
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                const unsigned other = corner ^ (1u << axis);
                if (other <= corner) continue;
                const auto a = corners[corner], b = corners[other];
                for (float plane : {0.0f, 0.0001f})
                    if ((a.w < plane && b.w > plane) || (b.w < plane && a.w > plane))
                    {
                        auto intersection = a + (b - a) * ((plane - a.w) / (b.w - a.w));
                        intersection.w = plane;
                        include(intersection);
                    }
            }
        if (!std::isfinite(low.x) || !std::isfinite(low.y) || !std::isfinite(high.x) || !std::isfinite(high.y))
            return fallback(GlassBoundsReason::NonFinite);
        // The shader's largest blur tap is 12 pixels. Add two more for
        // bilinear filtering and conservative rounding at snapshot boundaries.
        const glm::vec2 dimensions(width, height);
        const float guard = sampleFootprint ? 14.0f : 2.0f;
        low = glm::clamp(glm::floor(low * dimensions) - guard, glm::vec2(0), dimensions);
        high = glm::clamp(glm::ceil(high * dimensions) + guard, glm::vec2(0), dimensions);
        return {static_cast<std::int32_t>(low.x), static_cast<std::int32_t>(low.y),
                static_cast<std::uint32_t>(high.x - low.x), static_cast<std::uint32_t>(high.y - low.y)};
    }

    struct GlassSnapshotGroup
    {
        std::size_t end;
        rhi::Scissor bounds;
        GlassGroupBoundary boundary = GlassGroupBoundary::End;
    };

    inline bool GlassFootprintsOverlap(const rhi::Scissor &a, const rhi::Scissor &b)
    {
        return a.x < b.x + static_cast<std::int32_t>(b.width) &&
            b.x < a.x + static_cast<std::int32_t>(a.width) &&
            a.y < b.y + static_cast<std::int32_t>(b.height) &&
            b.y < a.y + static_cast<std::int32_t>(a.height);
    }

    // A later pane needs a fresh snapshot only when it samples pixels written
    // by an earlier pane. Overlapping read footprints alone are harmless.
    // Callers without raster bounds retain the conservative read/read test.
    // Bound group size to keep planning cost bounded for very large imports.
    inline GlassSnapshotGroup PlanGlassSnapshotGroup(std::span<const BasicDraw> draws,
        std::span<const rhi::Scissor> footprints, std::size_t first,
        std::span<const rhi::Scissor> rasterFootprints = {})
    {
        GlassSnapshotGroup group{first + 1, footprints[first]};
        const auto limit = std::min(draws.size(), first + std::size_t{64});
        while (group.end < limit && draws[group.end].surfaceType == 1u)
        {
            const auto &next = footprints[group.end];
            bool overlaps = false;
            for (auto index = first; index < group.end; ++index)
                if (GlassFootprintsOverlap(rasterFootprints.empty() ? footprints[index] : rasterFootprints[index], next))
                {
                    overlaps = true;
                    group.boundary = rasterFootprints.empty() || GlassFootprintsOverlap(rasterFootprints[index], rasterFootprints[group.end])
                        ? GlassGroupBoundary::RasterOverlap : GlassGroupBoundary::SampleOverlap;
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
        if (group.boundary == GlassGroupBoundary::End && group.end < draws.size())
            group.boundary = group.end == limit ? GlassGroupBoundary::Limit : GlassGroupBoundary::OtherSurface;
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
