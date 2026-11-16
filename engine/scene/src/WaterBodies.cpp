#include "PlutoGE/scene/WaterBodies.h"

#include "PlutoGE/scene/water/WaterSurfaceSnapshot.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <map>
#include <unordered_map>

namespace PlutoGE::scene
{
    namespace
    {
        float SmoothStep(float t)
        {
            t = std::clamp(t, 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        float DistanceToSegment(glm::vec2 point, glm::vec2 a, glm::vec2 b, float &t)
        {
            const glm::vec2 edge = b - a;
            const float lengthSquared = glm::dot(edge, edge);
            t = lengthSquared > 0.0f ? std::clamp(glm::dot(point - a, edge) / lengthSquared, 0.0f, 1.0f) : 0.0f;
            return glm::length(point - (a + edge * t));
        }

        // Douglas-Peucker on an open chain: keeps the points farther than `tolerance` from their simplified segment.
        void Simplify(std::span<const glm::vec2> points, std::size_t first, std::size_t last, float tolerance, std::vector<bool> &keep)
        {
            if (last <= first + 1)
                return;
            float farthest = -1.0f;
            std::size_t index = first;
            for (std::size_t i = first + 1; i < last; ++i)
            {
                float t = 0.0f;
                const float distance = DistanceToSegment(points[i], points[first], points[last], t);
                if (distance > farthest)
                    farthest = distance, index = i;
            }
            if (farthest <= tolerance)
                return;
            keep[index] = true;
            Simplify(points, first, index, tolerance, keep);
            Simplify(points, index, last, tolerance, keep);
        }

        std::vector<glm::vec2> SimplifyClosed(std::span<const glm::vec2> polygon, float tolerance)
        {
            const std::size_t count = polygon.size();
            // Anchor at the first point and the point farthest from it, and simplify both chains between them.
            std::size_t opposite = 0;
            float farthest = -1.0f;
            for (std::size_t i = 1; i < count; ++i)
                if (const float distance = glm::distance(polygon[0], polygon[i]); distance > farthest)
                    farthest = distance, opposite = i;
            std::vector<glm::vec2> ring(polygon.begin(), polygon.end());
            ring.push_back(polygon[0]);
            std::vector<bool> keep(ring.size(), false);
            keep[0] = keep[opposite] = keep[ring.size() - 1] = true;
            Simplify(ring, 0, opposite, tolerance, keep);
            Simplify(ring, opposite, ring.size() - 1, tolerance, keep);
            std::vector<glm::vec2> result;
            for (std::size_t i = 0; i + 1 < ring.size(); ++i)
                if (keep[i])
                    result.push_back(ring[i]);
            return result;
        }

    }

    float PolygonArea(std::span<const glm::vec2> polygon)
    {
        float area = 0.0f;
        for (std::size_t i = 0; i < polygon.size(); ++i)
        {
            const auto &a = polygon[i];
            const auto &b = polygon[(i + 1) % polygon.size()];
            area += a.x * b.y - b.x * a.y;
        }
        return 0.5f * area;
    }

    bool PointInPolygon(std::span<const glm::vec2> polygon, glm::vec2 point)
    {
        bool inside = false;
        for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++)
        {
            const auto &a = polygon[i];
            const auto &b = polygon[j];
            if ((a.y > point.y) != (b.y > point.y) && point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x)
                inside = !inside;
        }
        return polygon.size() >= 3 && inside;
    }

    std::vector<glm::vec2> SimplifyPolygon(std::span<const glm::vec2> polygon, std::size_t maxPoints)
    {
        maxPoints = std::max<std::size_t>(maxPoints, 3);
        if (polygon.size() <= maxPoints)
            return {polygon.begin(), polygon.end()};
        // The smallest tolerance that fits: a binary search over the polygon's size.
        glm::vec2 low(std::numeric_limits<float>::max()), high(std::numeric_limits<float>::lowest());
        for (const auto &point : polygon)
            low = glm::min(low, point), high = glm::max(high, point);
        float lowTolerance = 0.0f, highTolerance = glm::length(high - low);
        std::vector<glm::vec2> best = SimplifyClosed(polygon, highTolerance);
        for (int iteration = 0; iteration < 40; ++iteration)
        {
            const float tolerance = 0.5f * (lowTolerance + highTolerance);
            auto candidate = SimplifyClosed(polygon, tolerance);
            if (candidate.size() <= maxPoints)
            {
                best = std::move(candidate);
                highTolerance = tolerance;
            }
            else
                lowTolerance = tolerance;
        }
        if (best.size() > maxPoints)
            best.resize(maxPoints);
        return best;
    }

    std::vector<glm::vec2> TraceWaterOutline(const TerrainHeightField &heights, float cellSize, float level, glm::vec2 seed,
                                             const WaterOutlineOptions &options)
    {
        if (!heights.Valid() || cellSize <= 0.0f)
            return {};
        const int width = heights.width, depth = heights.depth;
        const int seedX = static_cast<int>(std::lround(seed.x / cellSize));
        const int seedZ = static_cast<int>(std::lround(seed.y / cellSize));
        if (seedX < 0 || seedZ < 0 || seedX >= width || seedZ >= depth || heights.At(seedX, seedZ) >= level)
            return {};

        // Flood the samples below the level that the seed reaches.
        std::vector<std::uint8_t> filled(static_cast<std::size_t>(width) * depth, 0);
        const auto index = [width](int x, int z) { return static_cast<std::size_t>(z) * width + x; };
        std::deque<std::pair<int, int>> queue{{seedX, seedZ}};
        filled[index(seedX, seedZ)] = 1;
        while (!queue.empty())
        {
            const auto [x, z] = queue.front();
            queue.pop_front();
            for (const auto [dx, dz] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}})
            {
                const int nx = x + dx, nz = z + dz;
                if (nx < 0 || nz < 0 || nx >= width || nz >= depth || filled[index(nx, nz)] || heights.At(nx, nz) >= level)
                    continue;
                filled[index(nx, nz)] = 1;
                queue.emplace_back(nx, nz);
            }
        }
        // Reach past the shore, under the banks.
        for (int pass = 0; pass < static_cast<int>(std::lround(std::max(options.shoreMargin, 0.0f))); ++pass)
        {
            auto grown = filled;
            for (int z = 0; z < depth; ++z)
                for (int x = 0; x < width; ++x)
                    if (!filled[index(x, z)] && ((x > 0 && filled[index(x - 1, z)]) || (x + 1 < width && filled[index(x + 1, z)]) ||
                                                 (z > 0 && filled[index(x, z - 1)]) || (z + 1 < depth && filled[index(x, z + 1)])))
                        grown[index(x, z)] = 1;
            filled.swap(grown);
        }

        // The boundary between filled and empty samples (each sample a unit square), as edges with the water on their
        // left, chained into loops. Corner (cx, cz) is at sample coordinates (cx - 0.5, cz - 0.5).
        const auto isFilled = [&](int x, int z) { return x >= 0 && z >= 0 && x < width && z < depth && filled[index(x, z)]; };
        const auto key = [](int cx, int cz) { return (static_cast<std::int64_t>(cx) << 32) ^ static_cast<std::uint32_t>(cz); };
        std::unordered_multimap<std::int64_t, std::pair<int, int>> edges; // start corner -> end corner
        for (int z = 0; z < depth; ++z)
            for (int x = 0; x < width; ++x)
            {
                if (!isFilled(x, z))
                    continue;
                if (!isFilled(x, z - 1)) edges.emplace(key(x, z), std::pair{x + 1, z});
                if (!isFilled(x + 1, z)) edges.emplace(key(x + 1, z), std::pair{x + 1, z + 1});
                if (!isFilled(x, z + 1)) edges.emplace(key(x + 1, z + 1), std::pair{x, z + 1});
                if (!isFilled(x - 1, z)) edges.emplace(key(x, z + 1), std::pair{x, z});
            }

        std::vector<glm::vec2> best;
        float bestArea = 0.0f;
        while (!edges.empty())
        {
            auto start = edges.begin();
            const std::int64_t startKey = start->first;
            std::vector<glm::vec2> loop;
            std::pair<int, int> corner{static_cast<int>(startKey >> 32), static_cast<int>(static_cast<std::uint32_t>(startKey))};
            for (std::size_t guard = 0; guard < 4u * filled.size() + 8; ++guard)
            {
                const auto found = edges.find(key(corner.first, corner.second));
                if (found == edges.end())
                    break;
                const auto next = found->second;
                edges.erase(found);
                // Only corners where the boundary turns matter.
                loop.push_back(glm::vec2(static_cast<float>(corner.first) - 0.5f, static_cast<float>(corner.second) - 0.5f));
                corner = next;
                if (key(corner.first, corner.second) == startKey)
                    break;
            }
            const float area = PolygonArea(loop);
            if (area > bestArea)
                bestArea = area, best = std::move(loop);
        }
        if (best.size() < 3)
            return {};

        // Drop the points along straight runs, then fit the point budget; then clamp to the grid and scale to units.
        std::vector<glm::vec2> corners;
        for (std::size_t i = 0; i < best.size(); ++i)
        {
            const auto &previous = best[(i + best.size() - 1) % best.size()];
            const auto &next = best[(i + 1) % best.size()];
            const glm::vec2 a = best[i] - previous, b = next - best[i];
            if (std::abs(a.x * b.y - a.y * b.x) > 1e-6f)
                corners.push_back(best[i]);
        }
        auto polygon = SimplifyPolygon(corners, static_cast<std::size_t>(std::max(options.maxPoints, 3)));
        const glm::vec2 limit(static_cast<float>(width - 1), static_cast<float>(depth - 1));
        for (auto &point : polygon)
            point = glm::clamp(point, glm::vec2(0.0f), limit) * cellSize;
        return polygon;
    }

    std::vector<glm::vec3> ConformRiverPath(std::span<const glm::vec3> points, const std::function<std::optional<float>(glm::vec2)> &ground,
                                            float surfaceDrop)
    {
        std::vector<glm::vec3> result(points.begin(), points.end());
        for (std::size_t i = 0; i < result.size(); ++i)
        {
            if (const auto height = ground ? ground({result[i].x, result[i].z}) : std::nullopt)
                result[i].y = *height - surfaceDrop;
            if (i > 0)
                result[i].y = std::min(result[i].y, result[i - 1].y);
        }
        return result;
    }

    TerrainSampleRect CarveRiverBed(TerrainHeightField &heights, float cellSize, std::span<const glm::vec3> path, const RiverBedShape &shape)
    {
        TerrainSampleRect changed;
        if (!heights.Valid() || path.size() < 2 || cellSize <= 0.0f)
            return changed;
        const float halfWidth = std::max(shape.halfWidth, 0.01f);
        const float reach = halfWidth + std::max(shape.bankWidth, 0.0f);
        glm::vec2 low(std::numeric_limits<float>::max()), high(std::numeric_limits<float>::lowest());
        for (const auto &point : path)
            low = glm::min(low, glm::vec2(point.x, point.z)), high = glm::max(high, glm::vec2(point.x, point.z));
        const int x0 = std::max(0, static_cast<int>(std::floor((low.x - reach) / cellSize)));
        const int z0 = std::max(0, static_cast<int>(std::floor((low.y - reach) / cellSize)));
        const int x1 = std::min(heights.width - 1, static_cast<int>(std::ceil((high.x + reach) / cellSize)));
        const int z1 = std::min(heights.depth - 1, static_cast<int>(std::ceil((high.y + reach) / cellSize)));
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x)
            {
                const glm::vec2 position(x * cellSize, z * cellSize);
                float nearest = std::numeric_limits<float>::max();
                float surface = 0.0f;
                for (std::size_t i = 0; i + 1 < path.size(); ++i)
                {
                    float t = 0.0f;
                    const float distance = DistanceToSegment(position, {path[i].x, path[i].z}, {path[i + 1].x, path[i + 1].z}, t);
                    if (distance < nearest)
                        nearest = distance, surface = path[i].y + (path[i + 1].y - path[i].y) * t;
                }
                if (nearest >= reach)
                    continue;
                float target;
                if (nearest <= halfWidth)
                {
                    // A rounded channel: full depth on the centre line, shallow (never dry) at the water's edge.
                    const float across = nearest / halfWidth;
                    target = surface - std::max(0.1f, shape.depth * (1.0f - across * across));
                }
                else
                    target = surface + (heights.At(x, z) - surface) * SmoothStep((nearest - halfWidth) / std::max(shape.bankWidth, 1e-3f));
                if (target < heights.At(x, z))
                {
                    heights.At(x, z) = target;
                    changed.Include({x, z, x + 1, z + 1});
                }
            }
        return changed;
    }

    std::optional<WaterSurfaceSample> SampleRiverPath(std::span<const glm::vec3> path, float halfWidth, float flowSpeed,
                                                      glm::vec3 worldPosition)
    {
        if (path.size() < 2 || halfWidth <= 0.0f)
            return std::nullopt;
        const glm::vec2 point(worldPosition.x, worldPosition.z);
        float nearest = std::numeric_limits<float>::max();
        WaterSurfaceSample sample;
        sample.kind = WaterBodyKind::River;
        for (std::size_t i = 0; i + 1 < path.size(); ++i)
        {
            float t = 0.0f;
            const float distance = DistanceToSegment(point, {path[i].x, path[i].z}, {path[i + 1].x, path[i + 1].z}, t);
            if (distance < nearest)
            {
                nearest = distance;
                sample.height = path[i].y + (path[i + 1].y - path[i].y) * t;
                const glm::vec3 direction = path[i + 1] - path[i];
                sample.flow = glm::length(direction) > 0.0f ? glm::normalize(direction) * flowSpeed : glm::vec3(0.0f);
            }
        }
        if (nearest > halfWidth)
            return std::nullopt;
        sample.stillHeight = sample.height;
        return sample;
    }

    std::optional<WaterSurfaceSample> SampleWaterSurface(const Scene &scene, glm::vec3 worldPosition)
    {
        return water::WaterSurfaceSnapshot::Capture(scene).Sample(worldPosition);
    }

    bool IsUnderwater(const Scene &scene, glm::vec3 worldPosition)
    {
        const auto surface = SampleWaterSurface(scene, worldPosition);
        return surface && worldPosition.y < surface->height;
    }
}
