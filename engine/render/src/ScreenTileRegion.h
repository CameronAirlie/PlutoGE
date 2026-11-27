#pragma once

#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/rhi/Types.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace PlutoGE::render
{
    // Part of a render target as disjoint rectangles, for full-screen passes that only some pixels need: they draw
    // once per rectangle under its scissor. Disjoint, so a pass that blends adds each pixel once. Empty means none.
    struct ScreenRegion
    {
        std::vector<rhi::Scissor> rectangles;

        static ScreenRegion Whole(std::uint32_t width, std::uint32_t height)
        {
            return width && height ? ScreenRegion{{rhi::Scissor{0, 0, width, height}}} : ScreenRegion{};
        }
        [[nodiscard]] bool Empty() const noexcept { return rectangles.empty(); }
        [[nodiscard]] std::uint64_t PixelCount() const noexcept
        {
            std::uint64_t pixels = 0;
            for (const auto &rectangle : rectangles)
                pixels += std::uint64_t(rectangle.width) * rectangle.height;
            return pixels;
        }
    };

    // Builds a ScreenRegion from world-space shapes: each marks the kTileSize-pixel tiles its screen bounds touch, and
    // covered tiles merge into rectangles (row runs, then equal runs downwards). Conservative: shapes that cross the
    // camera's eye plane cover the whole target.
    class ScreenTileGrid
    {
    public:
        static constexpr std::uint32_t kTileSize = 32;

        // `flipY` for render targets whose rows run top-down from clip-space y = +1 (Vulkan).
        ScreenTileGrid(const glm::mat4 &viewProjection, std::uint32_t width, std::uint32_t height, bool flipY)
            : m_viewProjection(viewProjection), m_width(width), m_height(height), m_flipY(flipY),
              m_columns((width + kTileSize - 1) / kTileSize), m_rows((height + kTileSize - 1) / kTileSize),
              m_tiles(std::size_t(m_columns) * m_rows, 0)
        {
        }

        // Marks the screen bounds of a convex hull's points.
        void MarkHull(std::span<const glm::vec3> points)
        {
            glm::vec2 low(std::numeric_limits<float>::max()), high(-std::numeric_limits<float>::max());
            bool front = false, behind = false;
            for (const auto &point : points)
            {
                const glm::vec4 clip = m_viewProjection * glm::vec4(point, 1.0f);
                if (!(clip.w > 1.0e-4f))
                {
                    behind = true;
                    continue;
                }
                front = true;
                glm::vec2 ndc = glm::vec2(clip) / clip.w;
                if (m_flipY) ndc.y = -ndc.y;
                low = glm::min(low, ndc);
                high = glm::max(high, ndc);
            }
            // Wholly behind the eye: out of view. Across the eye plane: part may be in view, so stay conservative.
            if (!front)
                return;
            if (behind)
            {
                MarkAll();
                return;
            }
            if (high.x < -1.0f || high.y < -1.0f || low.x > 1.0f || low.y > 1.0f)
                return;
            const glm::vec2 size(m_width, m_height);
            // A pixel's margin for rasterization rounding and temporal jitter.
            const glm::vec2 first = glm::clamp(glm::floor((low * 0.5f + 0.5f) * size) - 1.0f, glm::vec2(0.0f), size - 1.0f);
            const glm::vec2 last = glm::clamp(glm::ceil((high * 0.5f + 0.5f) * size) + 1.0f, glm::vec2(0.0f), size - 1.0f);
            for (auto y = std::uint32_t(first.y) / kTileSize; y <= std::uint32_t(last.y) / kTileSize; ++y)
                for (auto x = std::uint32_t(first.x) / kTileSize; x <= std::uint32_t(last.x) / kTileSize; ++x)
                    Mark(x, y);
        }

        // Marks a sphere, as the cube around it.
        void MarkSphere(const glm::vec3 &center, float radius)
        {
            const auto corners = CubeCorners(center, radius);
            MarkHull(corners);
        }

        // Marks what a sphere sweeps moving by `offset`: the hull of its cubes at both ends.
        void MarkSweptSphere(const glm::vec3 &center, float radius, const glm::vec3 &offset)
        {
            const auto start = CubeCorners(center, radius), end = CubeCorners(center + offset, radius);
            std::array<glm::vec3, 16> corners{};
            std::copy(start.begin(), start.end(), corners.begin());
            std::copy(end.begin(), end.end(), corners.begin() + 8);
            MarkHull(corners);
        }

        void MarkAll() noexcept
        {
            std::fill(m_tiles.begin(), m_tiles.end(), std::uint8_t{1});
            m_marked = m_tiles.size();
        }
        [[nodiscard]] bool Full() const noexcept { return m_marked == m_tiles.size(); }

        [[nodiscard]] ScreenRegion Region() const
        {
            if (Full())
                return ScreenRegion::Whole(m_width, m_height);
            ScreenRegion region;
            struct Run { std::uint32_t first, end; std::size_t rectangle; };
            std::vector<Run> above, current;
            for (std::uint32_t y = 0; y < m_rows; ++y)
            {
                current.clear();
                for (std::uint32_t x = 0; x < m_columns;)
                {
                    if (!Marked(x, y)) { ++x; continue; }
                    const std::uint32_t first = x;
                    while (x < m_columns && Marked(x, y)) ++x;
                    const auto match = std::find_if(above.begin(), above.end(),
                                                    [&](const Run &run) { return run.first == first && run.end == x; });
                    if (match != above.end())
                    {
                        auto &rectangle = region.rectangles[match->rectangle];
                        rectangle.height = PixelY(y + 1) - std::uint32_t(rectangle.y);
                        current.push_back(*match);
                        continue;
                    }
                    region.rectangles.push_back({std::int32_t(PixelX(first)), std::int32_t(PixelY(y)),
                                                 PixelX(x) - PixelX(first), PixelY(y + 1) - PixelY(y)});
                    current.push_back({first, x, region.rectangles.size() - 1});
                }
                above.swap(current);
            }
            return region;
        }

    private:
        static std::array<glm::vec3, 8> CubeCorners(const glm::vec3 &center, float radius)
        {
            std::array<glm::vec3, 8> corners{};
            for (unsigned corner = 0; corner < 8; ++corner)
                corners[corner] = center + radius * glm::vec3((corner & 1) ? 1.0f : -1.0f, (corner & 2) ? 1.0f : -1.0f,
                                                              (corner & 4) ? 1.0f : -1.0f);
            return corners;
        }
        void Mark(std::uint32_t x, std::uint32_t y)
        {
            auto &tile = m_tiles[std::size_t(std::min(y, m_rows - 1)) * m_columns + std::min(x, m_columns - 1)];
            m_marked += tile ? 0 : 1;
            tile = 1;
        }
        [[nodiscard]] bool Marked(std::uint32_t x, std::uint32_t y) const { return m_tiles[std::size_t(y) * m_columns + x] != 0; }
        [[nodiscard]] std::uint32_t PixelX(std::uint32_t tile) const { return std::min(tile * kTileSize, m_width); }
        [[nodiscard]] std::uint32_t PixelY(std::uint32_t tile) const { return std::min(tile * kTileSize, m_height); }

        glm::mat4 m_viewProjection;
        std::uint32_t m_width, m_height;
        bool m_flipY;
        std::uint32_t m_columns, m_rows;
        std::vector<std::uint8_t> m_tiles;
        std::size_t m_marked = 0;
    };

    // Calls `visit(center, radius)` with each world bounding sphere of a draw: one per instance for instanced draws
    // (BasicDraw::localBoundingSphere), else the draw's own. Returns false when the draw's extent is unknown: no
    // bounds, too many instances to project one by one, or wind or detail grass moving geometry past its bounds.
    template<class Visit>
    bool ForEachDrawSphere(const BasicDraw &draw, Visit &&visit)
    {
        constexpr std::size_t kMaxProjectedInstances = 256;
        if (draw.foliageWind.enabled || draw.detailGrass)
            return false;
        if (draw.instanceModels && !draw.instanceModels->empty())
        {
            const auto &local = draw.localBoundingSphere;
            if (local.w < 0.0f || draw.instanceModels->size() > kMaxProjectedInstances)
                return false;
            for (const auto &model : *draw.instanceModels)
            {
                const float scale = std::max({glm::length(glm::vec3(model[0])), glm::length(glm::vec3(model[1])),
                                              glm::length(glm::vec3(model[2]))});
                visit(glm::vec3(model * glm::vec4(glm::vec3(local), 1.0f)), local.w * scale);
            }
            return true;
        }
        if (draw.shadowBoundsRadius < 0.0f)
            return false;
        visit(draw.shadowBoundsCenter, draw.shadowBoundsRadius);
        return true;
    }
}
