#include "PlutoGE/render/LightmapUvs.h"

#include "PlutoGE/render/SkylinePacker.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <deque>
#include <numeric>
#include <unordered_map>
#include <vector>

namespace PlutoGE::render
{
    namespace
    {
        glm::vec3 Position(const MeshVertexData &vertex)
        {
            return {vertex.position[0], vertex.position[1], vertex.position[2]};
        }

        // The position charts are built from: scaled to the proportions the layout is fitted to.
        glm::vec3 ChartPosition(const MeshVertexData &vertex, const glm::vec3 &scale) { return Position(vertex) * scale; }

        struct Triangle
        {
            std::uint32_t corner[3]{};   // vertex indices
            std::uint32_t welded[3]{};   // position-welded ids, for adjacency across split vertices
            glm::vec3 normal{0.0f};
            float area = 0.0f;
            int chart = -1;
        };

        struct Chart
        {
            std::vector<std::size_t> triangles;
            glm::vec3 axis{0.0f};
            glm::vec2 boundsMin{0.0f}, boundsMax{0.0f};
            // 2D frame after the bounding-box rotation.
            glm::vec3 u{1, 0, 0}, v{0, 1, 0};
            PackPosition packed;
            PackSize cells;
        };

        void TangentFrame(const glm::vec3 &axis, glm::vec3 &u, glm::vec3 &v)
        {
            const glm::vec3 reference = std::abs(axis.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            u = glm::normalize(glm::cross(reference, axis));
            v = glm::cross(axis, u);
        }

        float Cross(const glm::vec2 &o, const glm::vec2 &a, const glm::vec2 &b)
        {
            return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
        }

        std::vector<glm::vec2> ConvexHull(std::vector<glm::vec2> points)
        {
            std::sort(points.begin(), points.end(), [](const glm::vec2 &a, const glm::vec2 &b) {
                return a.x != b.x ? a.x < b.x : a.y < b.y;
            });
            points.erase(std::unique(points.begin(), points.end()), points.end());
            if (points.size() < 3)
                return points;
            std::vector<glm::vec2> hull(points.size() * 2);
            std::size_t count = 0;
            for (const auto &point : points)
            {
                while (count >= 2 && Cross(hull[count - 2], hull[count - 1], point) <= 0.0f) --count;
                hull[count++] = point;
            }
            for (std::size_t index = points.size() - 1, lower = count + 1; index-- > 0;)
            {
                while (count >= lower && Cross(hull[count - 2], hull[count - 1], points[index]) <= 0.0f) --count;
                hull[count++] = points[index];
            }
            hull.resize(count - 1);
            return hull;
        }

        // Turns the chart's frame so its points' bounding box is smallest (the optimum has a hull edge on a side).
        void FitChartFrame(Chart &chart, const std::vector<Triangle> &triangles, const MeshData &data, const glm::vec3 &scale)
        {
            glm::vec3 u, v;
            TangentFrame(chart.axis, u, v);
            std::vector<glm::vec2> points;
            points.reserve(chart.triangles.size() * 3);
            for (const auto index : chart.triangles)
                for (const auto corner : triangles[index].corner)
                {
                    const auto p = ChartPosition(data.vertices[corner], scale);
                    points.push_back({glm::dot(p, u), glm::dot(p, v)});
                }
            const auto hull = ConvexHull(points);
            float bestArea = std::numeric_limits<float>::max();
            float bestAngle = 0.0f;
            const auto evaluate = [&](float angle) {
                const float c = std::cos(angle), s = std::sin(angle);
                glm::vec2 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
                for (const auto &p : hull.empty() ? points : hull)
                {
                    const glm::vec2 r(c * p.x + s * p.y, -s * p.x + c * p.y);
                    lo = glm::min(lo, r);
                    hi = glm::max(hi, r);
                }
                const float area = (hi.x - lo.x) * (hi.y - lo.y);
                if (area < bestArea)
                {
                    bestArea = area;
                    bestAngle = angle;
                }
            };
            evaluate(0.0f);
            for (std::size_t edge = 0; edge < hull.size(); ++edge)
            {
                const auto d = hull[(edge + 1) % hull.size()] - hull[edge];
                if (glm::dot(d, d) > 0.0f)
                    evaluate(std::atan2(d.y, d.x));
            }
            const float c = std::cos(bestAngle), s = std::sin(bestAngle);
            chart.u = u * c + v * s;
            chart.v = -u * s + v * c;
            chart.boundsMin = glm::vec2(std::numeric_limits<float>::max());
            chart.boundsMax = glm::vec2(std::numeric_limits<float>::lowest());
            for (const auto &p : points)
            {
                // points are in (u, v); rotate into the fitted frame.
                const glm::vec2 r(c * p.x + s * p.y, -s * p.x + c * p.y);
                chart.boundsMin = glm::min(chart.boundsMin, r);
                chart.boundsMax = glm::max(chart.boundsMax, r);
            }
        }
    }

    LightmapUvResult GenerateLightmapUvs(MeshData &data, std::span<const LightmapUvRange> ranges,
                                         const LightmapUvSettings &settings)
    {
        LightmapUvResult result;
        if (data.vertices.empty())
            return result;
        // Weld by position so charts grow across vertices split for normals or material UVs.
        glm::vec3 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
        for (const auto &vertex : data.vertices)
        {
            lo = glm::min(lo, Position(vertex));
            hi = glm::max(hi, Position(vertex));
        }
        const float weldScale = 1.0f / std::max(glm::length(hi - lo) * 1.0e-6f, 1.0e-9f);
        std::unordered_map<std::uint64_t, std::uint32_t> weldIds;
        std::vector<std::uint32_t> welded(data.vertices.size());
        for (std::size_t index = 0; index < data.vertices.size(); ++index)
        {
            const auto q = glm::i64vec3(glm::round((Position(data.vertices[index]) - lo) * weldScale));
            const std::uint64_t key = (std::uint64_t(q.x) * 73856093ull) ^ (std::uint64_t(q.y) * 19349663ull) ^ (std::uint64_t(q.z) * 83492791ull);
            welded[index] = weldIds.emplace(key, static_cast<std::uint32_t>(weldIds.size())).first->second;
        }

        const glm::vec3 scale = glm::max(glm::abs(settings.positionScale), glm::vec3(1.0e-4f));
        const float cosMaxAngle = std::cos(glm::radians(std::clamp(settings.maxChartAngleDegrees, 1.0f, 89.0f)));
        std::vector<bool> vertexAssigned(data.vertices.size(), false);
        const std::size_t originalVertexCount = data.vertices.size();

        for (const auto &range : ranges)
        {
            if (range.indexCount < 3 || std::size_t(range.indexOffset) + range.indexCount > data.indices.size())
                continue;
            // Triangles and their adjacency (shared welded edges).
            std::vector<Triangle> triangles;
            triangles.reserve(range.indexCount / 3);
            for (std::uint32_t offset = 0; offset + 2 < range.indexCount; offset += 3)
            {
                Triangle triangle;
                bool valid = true;
                for (int k = 0; k < 3; ++k)
                {
                    triangle.corner[k] = data.indices[range.indexOffset + offset + k];
                    valid = valid && triangle.corner[k] < originalVertexCount;
                }
                if (!valid)
                    continue;
                for (int k = 0; k < 3; ++k)
                    triangle.welded[k] = welded[triangle.corner[k]];
                const auto a = ChartPosition(data.vertices[triangle.corner[0]], scale);
                const auto cross = glm::cross(ChartPosition(data.vertices[triangle.corner[1]], scale) - a,
                                              ChartPosition(data.vertices[triangle.corner[2]], scale) - a);
                triangle.area = 0.5f * glm::length(cross);
                triangle.normal = triangle.area > 0.0f ? cross / (2.0f * triangle.area) : glm::vec3(0, 1, 0);
                triangles.push_back(triangle);
            }
            std::unordered_map<std::uint64_t, std::vector<std::size_t>> edges;
            const auto edgeKey = [](std::uint32_t a, std::uint32_t b) {
                return (std::uint64_t(std::min(a, b)) << 32) | std::max(a, b);
            };
            for (std::size_t index = 0; index < triangles.size(); ++index)
                for (int k = 0; k < 3; ++k)
                    edges[edgeKey(triangles[index].welded[k], triangles[index].welded[(k + 1) % 3])].push_back(index);

            // Grow charts from the largest unassigned triangle.
            std::vector<std::size_t> seeds(triangles.size());
            std::iota(seeds.begin(), seeds.end(), std::size_t{0});
            std::stable_sort(seeds.begin(), seeds.end(), [&](std::size_t a, std::size_t b) { return triangles[a].area > triangles[b].area; });
            std::vector<Chart> charts;
            for (const auto seed : seeds)
            {
                if (triangles[seed].chart >= 0)
                    continue;
                Chart chart;
                const int chartIndex = static_cast<int>(charts.size());
                const glm::vec3 seedNormal = triangles[seed].normal;
                std::deque<std::size_t> open{seed};
                triangles[seed].chart = chartIndex;
                while (!open.empty())
                {
                    const auto current = open.front();
                    open.pop_front();
                    chart.triangles.push_back(current);
                    chart.axis += triangles[current].normal * triangles[current].area;
                    for (int k = 0; k < 3; ++k)
                    {
                        const auto found = edges.find(edgeKey(triangles[current].welded[k], triangles[current].welded[(k + 1) % 3]));
                        for (const auto neighbour : found->second)
                        {
                            auto &candidate = triangles[neighbour];
                            if (candidate.chart >= 0 || candidate.area <= 0.0f)
                                continue;
                            if (glm::dot(candidate.normal, seedNormal) < cosMaxAngle)
                                continue;
                            candidate.chart = chartIndex;
                            open.push_back(neighbour);
                        }
                    }
                }
                const float axisLength = glm::length(chart.axis);
                chart.axis = axisLength > 0.0f ? chart.axis / axisLength : seedNormal;
                charts.push_back(std::move(chart));
            }
            // A triangle facing away from its chart's mean plane would fold over in projection: give it its own chart.
            std::vector<Chart> split;
            for (auto &chart : charts)
            {
                if (chart.triangles.size() <= 1)
                    continue;
                std::vector<std::size_t> kept;
                kept.reserve(chart.triangles.size());
                for (const auto index : chart.triangles)
                {
                    if (glm::dot(triangles[index].normal, chart.axis) < 0.1f)
                    {
                        Chart single;
                        single.triangles.push_back(index);
                        single.axis = triangles[index].normal;
                        split.push_back(std::move(single));
                    }
                    else
                        kept.push_back(index);
                }
                chart.triangles = std::move(kept);
            }
            for (auto &chart : split)
                charts.push_back(std::move(chart));
            std::erase_if(charts, [](const Chart &chart) { return chart.triangles.empty(); });
            for (auto &chart : charts)
                FitChartFrame(chart, triangles, data, scale);

            // Pack charts on a grid sized from their total area; the gutter is a share of the layout's side.
            double boxArea = 0.0;
            for (const auto &chart : charts)
            {
                const auto extent = glm::max(chart.boundsMax - chart.boundsMin, glm::vec2(0.0f));
                boxArea += double(extent.x) * extent.y;
            }
            if (boxArea <= 0.0)
                continue;
            constexpr double kGridSide = 512.0;
            const double cellsPerUnit = kGridSide / std::sqrt(boxArea);
            const auto gutterCells = static_cast<std::uint32_t>(std::ceil(std::clamp(settings.gutter, 0.001f, 0.2f) * kGridSide));
            std::vector<PackSize> sizes;
            sizes.reserve(charts.size());
            double cellArea = 0.0;
            std::uint32_t widest = 0;
            for (auto &chart : charts)
            {
                const auto extent = glm::max(chart.boundsMax - chart.boundsMin, glm::vec2(0.0f));
                chart.cells = {static_cast<std::uint32_t>(std::ceil(extent.x * cellsPerUnit)) + 2u * gutterCells,
                               static_cast<std::uint32_t>(std::ceil(extent.y * cellsPerUnit)) + 2u * gutterCells};
                sizes.push_back(chart.cells);
                cellArea += double(chart.cells.width) * chart.cells.height;
                widest = std::max(widest, chart.cells.width);
            }
            std::uint32_t stripWidth = std::max(widest, static_cast<std::uint32_t>(std::ceil(std::sqrt(cellArea) * 1.1)));
            std::uint32_t usedHeight = 0;
            auto positions = SkylinePacker::Pack(sizes, stripWidth, std::numeric_limits<std::uint32_t>::max(), &usedHeight);
            if (!positions)
                continue;
            const double side = std::max<double>(stripWidth, usedHeight);

            // Write uv2, duplicating vertices another chart already claimed.
            std::unordered_map<std::uint64_t, std::uint32_t> remap;
            for (std::size_t chartIndex = 0; chartIndex < charts.size(); ++chartIndex)
            {
                const auto &chart = charts[chartIndex];
                const auto origin = (*positions)[chartIndex];
                for (const auto triangleIndex : chart.triangles)
                {
                    auto &triangle = triangles[triangleIndex];
                    for (int k = 0; k < 3; ++k)
                    {
                        const auto source = triangle.corner[k];
                        const std::uint64_t key = (std::uint64_t(source) << 32) | std::uint32_t(chartIndex);
                        auto found = remap.find(key);
                        if (found == remap.end())
                        {
                            std::uint32_t target = source;
                            if (vertexAssigned[source])
                            {
                                target = static_cast<std::uint32_t>(data.vertices.size());
                                const auto copy = data.vertices[source];
                                data.vertices.push_back(copy);
                                vertexAssigned.push_back(true);
                                ++result.addedVertices;
                            }
                            vertexAssigned[target] = true;
                            const auto p = ChartPosition(data.vertices[target], scale);
                            const glm::vec2 local(glm::dot(p, chart.u) - chart.boundsMin.x, glm::dot(p, chart.v) - chart.boundsMin.y);
                            const glm::dvec2 cell = glm::dvec2(origin.x + gutterCells, origin.y + gutterCells) + glm::dvec2(local) * cellsPerUnit;
                            data.vertices[target].uv2 = {static_cast<float>(std::clamp(cell.x / side, 0.0, 1.0)),
                                                         static_cast<float>(std::clamp(cell.y / side, 0.0, 1.0))};
                            found = remap.emplace(key, target).first;
                        }
                        triangle.corner[k] = found->second;
                    }
                }
            }
            // Rewrite the range's indices in its original triangle order.
            std::size_t written = 0;
            for (std::uint32_t offset = 0; offset + 2 < range.indexCount; offset += 3)
            {
                bool valid = true;
                for (int k = 0; k < 3; ++k)
                    valid = valid && data.indices[range.indexOffset + offset + k] < originalVertexCount;
                if (!valid)
                    continue;
                const auto &triangle = triangles[written++];
                for (int k = 0; k < 3; ++k)
                    data.indices[range.indexOffset + offset + k] = triangle.corner[k];
            }
            result.charts += charts.size();
        }
        return result;
    }

    LightmapUvResult GenerateLightmapUvs(MeshData &data, std::span<const Submesh> submeshes, const LightmapUvSettings &settings)
    {
        std::vector<LightmapUvRange> ranges;
        if (submeshes.empty())
            ranges.push_back({0, static_cast<std::uint32_t>(data.indices.size())});
        for (const auto &submesh : submeshes)
            ranges.push_back({submesh.indexOffset, submesh.indexCount});
        return GenerateLightmapUvs(data, ranges, settings);
    }

    LightmapUvCheck CheckLightmapUvs(const MeshData &data, LightmapUvRange range)
    {
        LightmapUvCheck check;
        check.boundsMin = glm::vec2(std::numeric_limits<float>::max());
        check.boundsMax = glm::vec2(std::numeric_limits<float>::lowest());
        constexpr float tolerance = 1.0e-4f;
        for (std::uint32_t offset = 0; offset + 2 < range.indexCount; offset += 3)
        {
            glm::vec2 uv[3];
            bool valid = std::size_t(range.indexOffset) + offset + 2 < data.indices.size();
            for (int k = 0; k < 3 && valid; ++k)
            {
                const auto index = data.indices[range.indexOffset + offset + k];
                valid = index < data.vertices.size();
                if (valid)
                    uv[k] = {data.vertices[index].uv2[0], data.vertices[index].uv2[1]};
            }
            if (!valid)
                continue;
            ++check.triangles;
            bool outside = false;
            for (const auto &point : uv)
            {
                outside = outside || point.x < -tolerance || point.y < -tolerance || point.x > 1.0f + tolerance || point.y > 1.0f + tolerance;
                check.boundsMin = glm::min(check.boundsMin, point);
                check.boundsMax = glm::max(check.boundsMax, point);
            }
            check.outside += outside ? 1 : 0;
            const float area = 0.5f * std::abs((uv[1].x - uv[0].x) * (uv[2].y - uv[0].y) - (uv[1].y - uv[0].y) * (uv[2].x - uv[0].x));
            if (area < 1.0e-10f)
                ++check.degenerate;
            check.uvArea += area;
        }
        if (check.triangles == 0)
            check.boundsMin = check.boundsMax = glm::vec2(0.0f);
        return check;
    }
}
