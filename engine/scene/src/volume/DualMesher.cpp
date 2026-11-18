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
            [[nodiscard]] bool Holds(glm::ivec3 sample) const { return m_block.box.Contains(sample); }
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

        glm::vec3 Normalised(const glm::vec3 &vector, const glm::vec3 &fallback)
        {
            const float length = glm::length(vector);
            return length > 1.0e-6f ? vector / length : fallback;
        }

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

        // How open the surface is around a cell: the share of air among the samples within `reach` of it, scaled so a
        // flat surface (half air) reads as fully open.
        float Openness(const BlockView &view, glm::ivec3 cell, int reach)
        {
            if (reach <= 0)
                return 1.0f;
            int air = 0, total = 0;
            for (int z = -reach + 1; z <= reach; ++z)
                for (int y = -reach + 1; y <= reach; ++y)
                    for (int x = -reach + 1; x <= reach; ++x)
                    {
                        const glm::ivec3 sample = cell + glm::ivec3(x, y, z);
                        if (!view.Holds(sample))
                            continue;
                        ++total;
                        air += IsRock(view.Distance(sample)) ? 0 : 1;
                    }
            return total > 0 ? glm::clamp(2.0f * static_cast<float>(air) / static_cast<float>(total), 0.0f, 1.0f) : 1.0f;
        }
    }

    VolumeMesh DualMesher::Mesh(const DensityBlock &block, const VolumeSampleBox &region, const VolumeGrid &grid, const glm::vec3 &origin) const
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

        // One vertex per cell the surface crosses, placed by the implementation from the cell's crossings.
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

                    Crossings crossings;
                    for (const auto &[a, b] : Edges)
                    {
                        if (IsRock(distance[a]) == IsRock(distance[b]))
                            continue;
                        const float t = distance[a] / (distance[a] - distance[b]);
                        crossings.points[crossings.count] = glm::mix(glm::vec3(Corners[a]), glm::vec3(Corners[b]), t);
                        crossings.normals[crossings.count] =
                            Normalised(glm::mix(view.Gradient(cell + Corners[a]), view.Gradient(cell + Corners[b]), t), glm::vec3(0.0f, 1.0f, 0.0f));
                        ++crossings.count;
                    }
                    const glm::vec3 inCell = glm::clamp(PlaceVertex(crossings), glm::vec3(0.0f), glm::vec3(1.0f));

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
                    const glm::vec3 normal = Normalised(gradient, glm::vec3(0.0f, 1.0f, 0.0f));
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
                    result.occlusion.push_back(Openness(view, cell, m_occlusionReach));
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

    glm::vec3 SurfaceNetsMesher::PlaceVertex(const Crossings &crossings) const
    {
        glm::vec3 sum(0.0f);
        for (int index = 0; index < crossings.count; ++index)
            sum += crossings.points[index];
        return crossings.count > 0 ? sum / static_cast<float>(crossings.count) : glm::vec3(0.5f);
    }

    glm::vec3 DualContouringMesher::PlaceVertex(const Crossings &crossings) const
    {
        if (crossings.count == 0)
            return glm::vec3(0.5f);
        glm::vec3 mass(0.0f);
        for (int index = 0; index < crossings.count; ++index)
            mass += crossings.points[index];
        mass /= static_cast<float>(crossings.count);

        // Minimise sum((n . (x - p))^2) + w |x - mass|^2, solved about the mass point: (A^T A + w I) d = A^T b.
        glm::mat3 ata(0.0f);
        glm::vec3 atb(0.0f);
        for (int index = 0; index < crossings.count; ++index)
        {
            const glm::vec3 &n = crossings.normals[index];
            ata += glm::outerProduct(n, n);
            atb += n * glm::dot(n, crossings.points[index] - mass);
        }
        const float w = std::max(m_regularisation, 1.0e-4f) * static_cast<float>(crossings.count);
        ata += glm::mat3(w);
        const float determinant = glm::determinant(ata);
        if (!(std::abs(determinant) > 1.0e-8f))
            return mass;
        const glm::vec3 offset = glm::inverse(ata) * atb;
        const glm::vec3 placed = mass + offset;
        // A fit far outside the cell is a poor one (nearly parallel planes): the mass point is safer.
        return glm::all(glm::greaterThanEqual(placed, glm::vec3(-0.5f))) && glm::all(glm::lessThanEqual(placed, glm::vec3(1.5f))) ? placed : mass;
    }

    std::shared_ptr<const VolumeMesher> MakeVolumeMesher(VolumeMesherKind kind)
    {
        switch (kind)
        {
        case VolumeMesherKind::DualContouring: return std::make_shared<DualContouringMesher>();
        case VolumeMesherKind::SurfaceNets: break;
        }
        return std::make_shared<SurfaceNetsMesher>();
    }

    VolumeMesh MeshRemovedRock(const TerrainVolume &volume, const VolumeShape &shape, const VolumeMesher &mesher, const glm::vec3 &origin)
    {
        const auto &grid = volume.GetGrid();
        // The shape's samples plus one: the outermost layer is forced to air, so the surface closes inside the region.
        auto region = grid.SamplesIn(shape.BoundsMin(), shape.BoundsMax());
        region.first -= 1;
        region.count += 2;
        auto block = volume.Gather(mesher.RequiredSamples(region));
        const VolumeSampleBox inner{region.first + 1, region.count - 2};
        bool rock = false;
        for (int z = 0; z < block.box.count.z; ++z)
            for (int y = 0; y < block.box.count.y; ++y)
                for (int x = 0; x < block.box.count.x; ++x)
                {
                    const glm::ivec3 sample = block.box.first + glm::ivec3(x, y, z);
                    auto &value = block.samples[block.Index({x, y, z})];
                    // Rock where both the volume and the shape are (an intersection), air outside the inner box.
                    value.distance = inner.Contains(sample) ? std::max(value.distance, shape.Distance(grid.SamplePosition(sample)))
                                                            : std::max(value.distance, grid.voxelSize);
                    rock = rock || value.distance < 0.0f;
                }
        return rock ? mesher.Mesh(block, region, grid, origin) : VolumeMesh{};
    }

    VolumeMesh MeshVolumeChunk(const TerrainVolume &volume, VolumeChunkCoord chunk, const VolumeMesher &mesher)
    {
        const auto &grid = volume.GetGrid();
        const auto region = grid.ChunkSamples(chunk);
        return mesher.Mesh(volume.Gather(mesher.RequiredSamples(region)), region, grid, grid.ChunkOrigin(chunk));
    }
}
