#include "PlutoGE/scene/volume/VolumeMesher.h"

#include <array>
#include <cmath>
#include <limits>

namespace PlutoGE::scene::volume
{
    namespace
    {
        // Corner i of a cell sits at (i & 1, (i >> 1) & 1, (i >> 2) & 1).
        constexpr std::array<glm::ivec3, 8> Corners{{
            {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}, {0, 0, 1}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1},
        }};
        // The twelve cell edges as corner pairs.
        constexpr std::array<std::array<int, 2>, 12> Edges{{
            {0, 1}, {2, 3}, {4, 5}, {6, 7}, // along x
            {0, 2}, {1, 3}, {4, 6}, {5, 7}, // along y
            {0, 4}, {1, 5}, {2, 6}, {3, 7}, // along z
        }};

        bool IsRock(float distance) { return distance < 0.0f; }

        // The block's samples addressed by global sample coordinate.
        class BlockView
        {
        public:
            explicit BlockView(const DensityBlock &block) : m_block(block) {}
            [[nodiscard]] const VolumeSample &At(glm::ivec3 sample) const { return m_block.At(sample - m_block.box.first); }
            [[nodiscard]] float Distance(glm::ivec3 sample) const { return At(sample).distance; }
            // Central differences: points towards the air.
            [[nodiscard]] glm::vec3 Gradient(glm::ivec3 sample) const
            {
                return {Distance(sample + glm::ivec3(1, 0, 0)) - Distance(sample - glm::ivec3(1, 0, 0)),
                        Distance(sample + glm::ivec3(0, 1, 0)) - Distance(sample - glm::ivec3(0, 1, 0)),
                        Distance(sample + glm::ivec3(0, 0, 1)) - Distance(sample - glm::ivec3(0, 0, 1))};
            }

        private:
            const DensityBlock &m_block;
        };

        glm::vec3 Tangent(const glm::vec3 &normal)
        {
            const glm::vec3 reference = std::abs(normal.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
            return glm::normalize(glm::cross(reference, normal));
        }

        bool Contains(const VolumeSampleBox &outer, const VolumeSampleBox &inner)
        {
            return glm::all(glm::lessThanEqual(outer.first, inner.first)) &&
                   glm::all(glm::greaterThanEqual(outer.first + outer.count, inner.first + inner.count));
        }
    }

    VolumeMesh SurfaceNetsMesher::Mesh(const DensityBlock &block, const VolumeSampleBox &region, const VolumeGrid &grid,
                                       const glm::vec3 &origin) const
    {
        VolumeMesh result;
        if (region.Empty() || !grid.Valid() || block.samples.size() != block.box.SampleCount() || !Contains(block.box, RequiredSamples(region)))
            return result;
        const BlockView view(block);

        // Cells whose vertices the region's quads use: one sample lower than the region on each axis, up to its last.
        const VolumeSampleBox cells{region.first - 1, region.count + 1};
        std::vector<std::int32_t> cellVertex(cells.SampleCount(), -1);
        const auto cellIndex = [&](glm::ivec3 cell) {
            const glm::ivec3 local = cell - cells.first;
            return (static_cast<std::size_t>(local.z) * cells.count.y + static_cast<std::size_t>(local.y)) * cells.count.x +
                   static_cast<std::size_t>(local.x);
        };

        auto &vertices = result.mesh.vertices;
        result.boundsMin = glm::vec3(std::numeric_limits<float>::max());
        result.boundsMax = glm::vec3(std::numeric_limits<float>::lowest());

        // One vertex per cell the surface crosses: the average of its edges' crossings.
        for (int z = 0; z < cells.count.z; ++z)
            for (int y = 0; y < cells.count.y; ++y)
                for (int x = 0; x < cells.count.x; ++x)
                {
                    const glm::ivec3 cell = cells.first + glm::ivec3(x, y, z);
                    std::array<float, 8> distance{};
                    int rockCorners = 0;
                    for (int corner = 0; corner < 8; ++corner)
                    {
                        distance[corner] = view.Distance(cell + Corners[corner]);
                        rockCorners += IsRock(distance[corner]) ? 1 : 0;
                    }
                    if (rockCorners == 0 || rockCorners == 8)
                        continue;

                    glm::vec3 crossing(0.0f);
                    int crossings = 0;
                    for (const auto &[a, b] : Edges)
                    {
                        if (IsRock(distance[a]) == IsRock(distance[b]))
                            continue;
                        const float t = distance[a] / (distance[a] - distance[b]);
                        crossing += glm::mix(glm::vec3(Corners[a]), glm::vec3(Corners[b]), t);
                        ++crossings;
                    }
                    const glm::vec3 inCell = crossing / static_cast<float>(crossings);

                    // The normal: the distance gradient at the corners, blended trilinearly to the vertex.
                    glm::vec3 gradient(0.0f);
                    int deepest = 0;
                    for (int corner = 0; corner < 8; ++corner)
                    {
                        const glm::vec3 weight = glm::mix(glm::vec3(1.0f) - inCell, inCell, glm::vec3(Corners[corner]));
                        gradient += view.Gradient(cell + Corners[corner]) * (weight.x * weight.y * weight.z);
                        if (distance[corner] < distance[deepest])
                            deepest = corner;
                    }
                    const float length = glm::length(gradient);
                    const glm::vec3 normal = length > 1.0e-6f ? gradient / length : glm::vec3(0.0f, 1.0f, 0.0f);
                    const glm::vec3 position = grid.SamplePosition(cell) + inCell * grid.voxelSize - origin;
                    const glm::vec3 tangent = Tangent(normal);

                    render::MeshVertexData vertex{};
                    vertex.position = {position.x, position.y, position.z};
                    vertex.normal = {normal.x, normal.y, normal.z};
                    vertex.uv = {0.0f, 0.0f};
                    vertex.tangent = {tangent.x, tangent.y, tangent.z, 1.0f};
                    cellVertex[cellIndex(cell)] = static_cast<std::int32_t>(vertices.size());
                    vertices.push_back(vertex);
                    result.materials.push_back(view.At(cell + Corners[deepest]).material);
                    result.boundsMin = glm::min(result.boundsMin, position);
                    result.boundsMax = glm::max(result.boundsMax, position);
                }

        // One quad per crossed edge the region owns, joining the four cells around the edge. For an edge along axis a,
        // with u and v the next axes in cyclic order (u x v = a), the cells at (-u, -v), (0, -v), (0, 0), (-u, 0) run
        // counter-clockwise seen from +a: the order when the air is on the +a side.
        auto &indices = result.mesh.indices;
        const auto quad = [&](std::array<std::int32_t, 4> corner, bool airOnPositiveSide) {
            if (!airOnPositiveSide)
                std::swap(corner[1], corner[3]);
            // Split along the shorter diagonal: better shaped triangles, the same winding either way.
            const auto position = [&](int index) { return glm::vec3(vertices[corner[index]].position[0], vertices[corner[index]].position[1],
                                                                    vertices[corner[index]].position[2]); };
            const auto emit = [&](int a, int b, int c) {
                indices.push_back(static_cast<unsigned>(corner[a]));
                indices.push_back(static_cast<unsigned>(corner[b]));
                indices.push_back(static_cast<unsigned>(corner[c]));
            };
            if (glm::distance(position(0), position(2)) <= glm::distance(position(1), position(3)))
            {
                emit(0, 1, 2);
                emit(0, 2, 3);
            }
            else
            {
                emit(0, 1, 3);
                emit(1, 2, 3);
            }
        };
        constexpr std::array<glm::ivec3, 3> Axis{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
        for (int z = 0; z < region.count.z; ++z)
            for (int y = 0; y < region.count.y; ++y)
                for (int x = 0; x < region.count.x; ++x)
                {
                    const glm::ivec3 sample = region.first + glm::ivec3(x, y, z);
                    const bool rock = IsRock(view.Distance(sample));
                    for (int a = 0; a < 3; ++a)
                    {
                        if (IsRock(view.Distance(sample + Axis[a])) == rock)
                            continue;
                        const glm::ivec3 u = Axis[(a + 1) % 3];
                        const glm::ivec3 v = Axis[(a + 2) % 3];
                        const std::array<std::int32_t, 4> around{cellVertex[cellIndex(sample - u - v)], cellVertex[cellIndex(sample - v)],
                                                                 cellVertex[cellIndex(sample)], cellVertex[cellIndex(sample - u)]};
                        // Every cell around a crossed edge has a vertex (the edge is one of its own).
                        quad(around, rock);
                    }
                }

        if (vertices.empty())
            result.boundsMin = result.boundsMax = glm::vec3(0.0f);
        return result;
    }

    VolumeMesh MeshVolumeChunk(const TerrainVolume &volume, VolumeChunkCoord chunk, const VolumeMesher &mesher)
    {
        const auto &grid = volume.GetGrid();
        const auto region = grid.ChunkSamples(chunk);
        return mesher.Mesh(volume.Gather(mesher.RequiredSamples(region)), region, grid, grid.ChunkOrigin(chunk));
    }
}
