#include "PlutoGE/scene/volume/TerrainVolumeCoupling.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace PlutoGE::scene::volume
{
    TerrainSurface::TerrainSurface(int width, int depth, float cellSize, std::vector<float> heights)
        : m_width(width), m_depth(depth), m_cellSize(cellSize), m_heights(std::move(heights))
    {
    }

    bool TerrainSurface::Valid() const
    {
        return m_width >= 2 && m_depth >= 2 && m_cellSize > 0.0f && m_heights.size() == static_cast<std::size_t>(m_width) * m_depth;
    }

    std::optional<float> TerrainSurface::Height(float x, float z) const
    {
        if (!Valid() || !std::isfinite(x) || !std::isfinite(z))
            return std::nullopt;
        const float fx = x / m_cellSize, fz = z / m_cellSize;
        // A hair of tolerance keeps the far edges (x = (width - 1) * cellSize) on the terrain.
        constexpr float edge = 1.0e-4f;
        if (fx < -edge || fz < -edge || fx > static_cast<float>(m_width - 1) + edge || fz > static_cast<float>(m_depth - 1) + edge)
            return std::nullopt;
        const int cx = std::clamp(static_cast<int>(std::floor(fx)), 0, m_width - 2);
        const int cz = std::clamp(static_cast<int>(std::floor(fz)), 0, m_depth - 2);
        const float tx = std::clamp(fx - static_cast<float>(cx), 0.0f, 1.0f);
        const float tz = std::clamp(fz - static_cast<float>(cz), 0.0f, 1.0f);
        const float h00 = Sample(cx, cz), h10 = Sample(cx + 1, cz), h01 = Sample(cx, cz + 1), h11 = Sample(cx + 1, cz + 1);
        // The terrain's triangles: (x, z) (x + 1, z) (x, z + 1), and (x + 1, z + 1) (x, z + 1) (x + 1, z).
        if (tx + tz <= 1.0f)
            return h00 + (h10 - h00) * tx + (h01 - h00) * tz;
        return h11 + (h01 - h11) * (1.0f - tx) + (h10 - h11) * (1.0f - tz);
    }

    glm::vec2 TerrainSurface::CellHeightRange(int cellX, int cellZ) const
    {
        const int x = std::clamp(cellX, 0, m_width - 2), z = std::clamp(cellZ, 0, m_depth - 2);
        const float a = Sample(x, z), b = Sample(x + 1, z), c = Sample(x, z + 1), d = Sample(x + 1, z + 1);
        return {std::min({a, b, c, d}), std::max({a, b, c, d})};
    }

    std::shared_ptr<const VolumeDensitySource> MakeTerrainDensity(std::shared_ptr<const TerrainSurface> surface, VolumeMaterial material)
    {
        return std::make_shared<HeightfieldDensity>(
            [surface = std::move(surface)](float x, float z) { return surface->Height(x, z); }, material);
    }

    namespace
    {
        // The cells whose closed area holds a coordinate along one axis: one, or two on a shared edge.
        std::pair<int, int> CellsHolding(float coordinate, float cellSize)
        {
            const float cells = coordinate / cellSize;
            const float nearest = std::round(cells);
            if (std::abs(cells - nearest) < 1.0e-4f)
                return {static_cast<int>(nearest) - 1, static_cast<int>(nearest)};
            const int cell = static_cast<int>(std::floor(cells));
            return {cell, cell};
        }

        bool IsCut(const TerrainHoleMask &holes, int cellX, int cellZ)
        {
            return holes.GetCut(cellX, cellZ) != TerrainHoleCut::None;
        }
    }

    TerrainHoleMask ComputeVolumeHoles(const TerrainVolume &volume, const TerrainSurface &surface, const TerrainCouplingSettings &settings)
    {
        TerrainHoleMask holes(std::max(surface.GetCellsX(), 0), std::max(surface.GetCellsZ(), 0));
        if (!surface.Valid() || volume.GetStoredChunkCount() == 0)
            return holes;
        const auto &grid = volume.GetGrid();
        const float tolerance = settings.breachTolerance * grid.voxelSize;

        // Every sample column of a stored chunk, once.
        std::set<std::pair<int, int>> columns;
        for (const auto chunk : volume.GetStoredChunks())
        {
            const auto first = grid.ChunkFirstSample(chunk);
            for (int z = 0; z < grid.chunkVoxels; ++z)
                for (int x = 0; x < grid.chunkVoxels; ++x)
                    columns.insert({first.x + x, first.z + z});
        }

        for (const auto &[sampleX, sampleZ] : columns)
        {
            const glm::vec3 column = grid.SamplePosition({sampleX, 0, sampleZ});
            const auto height = surface.Height(column.x, column.z);
            if (!height)
                continue;
            // The samples either side of the surface (and one more below: a sample exactly on it is air).
            const int surfaceSample = static_cast<int>(std::floor((*height - grid.origin.y) / grid.voxelSize));
            bool breached = false;
            for (int y = surfaceSample - 1; y <= surfaceSample + 1 && !breached; ++y)
            {
                const glm::ivec3 sample(sampleX, y, sampleZ);
                const float base = DequantizeDistance(QuantizeDistance(volume.GetBase().Sample(grid.SamplePosition(sample)).distance,
                                                                       grid.voxelSize),
                                                      grid.voxelSize);
                breached = std::abs(volume.Sample(sample).distance - base) > tolerance;
            }
            if (!breached)
                continue;
            const auto [x0, x1] = CellsHolding(column.x, surface.GetCellSize());
            const auto [z0, z1] = CellsHolding(column.z, surface.GetCellSize());
            for (int z = z0; z <= z1; ++z)
                for (int x = x0; x <= x1; ++x)
                    if (x >= 0 && z >= 0 && x < holes.GetCellsX() && z < holes.GetCellsZ())
                        holes.Set(x, z, true);
        }
        return holes;
    }

    void FitVolumeMeshToTerrain(VolumeMesh &mesh, const glm::vec3 &meshOrigin, const TerrainSurface &surface,
                                const TerrainHoleMask &volumeHoles, const TerrainCouplingSettings &settings, float voxelSize)
    {
        if (mesh.Empty() || !surface.Valid())
            return;
        const float cellSize = surface.GetCellSize();
        const float tolerance = settings.surfaceTolerance * voxelSize;
        const int ring = std::max(settings.overlapCells, 0);
        auto &vertices = mesh.mesh.vertices;
        const auto local = [&](unsigned int index) {
            const auto &position = vertices[index].position;
            return meshOrigin + glm::vec3(position[0], position[1], position[2]);
        };
        const auto cellOf = [&](const glm::vec3 &point) {
            return glm::ivec2(static_cast<int>(std::floor(point.x / cellSize)), static_cast<int>(std::floor(point.z / cellSize)));
        };
        const auto onSurface = [&](const glm::vec3 &point) {
            const auto height = surface.Height(point.x, point.z);
            return height && std::abs(point.y - *height) <= tolerance;
        };
        const auto nearCut = [&](glm::ivec2 cell, int reach) {
            for (int dz = -reach; dz <= reach; ++dz)
                for (int dx = -reach; dx <= reach; ++dx)
                    if (IsCut(volumeHoles, cell.x + dx, cell.y + dz))
                        return true;
            return false;
        };
        // Horizontal distance from a point to the nearest cut cell within the ring (or more).
        const auto distanceToCut = [&](const glm::vec3 &point) {
            const glm::vec2 p(point.x, point.z);
            const auto cell = cellOf(point);
            float best = std::numeric_limits<float>::max();
            for (int dz = -ring - 1; dz <= ring + 1; ++dz)
                for (int dx = -ring - 1; dx <= ring + 1; ++dx)
                    if (IsCut(volumeHoles, cell.x + dx, cell.y + dz))
                    {
                        const glm::vec2 low = glm::vec2(cell.x + dx, cell.y + dz) * cellSize;
                        const glm::vec2 outside = glm::max(glm::max(low - p, p - (low + cellSize)), glm::vec2(0.0f));
                        best = std::min(best, glm::length(outside));
                    }
            return best;
        };

        std::vector<unsigned int> kept;
        kept.reserve(mesh.mesh.indices.size());
        const auto &indices = mesh.mesh.indices;
        for (std::size_t index = 0; index + 2 < indices.size(); index += 3)
        {
            const glm::vec3 a = local(indices[index]), b = local(indices[index + 1]), c = local(indices[index + 2]);
            const glm::vec3 centre = (a + b + c) / 3.0f;
            const auto cell = cellOf(centre);
            const bool keep = !surface.Height(centre.x, centre.z)                        // off the terrain: only the volume
                              || IsCut(volumeHoles, cell.x, cell.y)                       // the heightfield is cut here
                              || !(onSurface(a) && onSurface(b) && onSurface(c))          // a cave, an overhang, a pit wall
                              || (ring > 0 && nearCut(cell, ring));                      // the overlap ring
            if (keep)
                kept.insert(kept.end(), {indices[index], indices[index + 1], indices[index + 2]});
        }

        // Only the vertices the kept triangles use, in their first-use order; ring vertices sink.
        constexpr unsigned int unused = std::numeric_limits<unsigned int>::max();
        std::vector<unsigned int> remap(vertices.size(), unused);
        render::MeshData fitted;
        std::vector<VolumeMaterial> materials;
        mesh.boundsMin = glm::vec3(std::numeric_limits<float>::max());
        mesh.boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
        for (auto &index : kept)
        {
            if (remap[index] == unused)
            {
                remap[index] = static_cast<unsigned int>(fitted.vertices.size());
                auto vertex = vertices[index];
                const glm::vec3 point = local(index);
                const auto cell = cellOf(point);
                if (settings.overlapSink > 0.0f && onSurface(point) && !IsCut(volumeHoles, cell.x, cell.y))
                {
                    const float ramp = voxelSize > 0.0f ? std::clamp(distanceToCut(point) / voxelSize, 0.0f, 1.0f) : 1.0f;
                    vertex.position[1] -= settings.overlapSink * ramp;
                }
                const glm::vec3 position(vertex.position[0], vertex.position[1], vertex.position[2]);
                mesh.boundsMin = glm::min(mesh.boundsMin, position);
                mesh.boundsMax = glm::max(mesh.boundsMax, position);
                fitted.vertices.push_back(vertex);
                materials.push_back(index < mesh.materials.size() ? mesh.materials[index] : VolumeMaterial{0});
            }
            index = remap[index];
        }
        fitted.indices = std::move(kept);
        if (fitted.vertices.empty())
            mesh.boundsMin = mesh.boundsMax = glm::vec3(0.0f);
        mesh.mesh = std::move(fitted);
        mesh.materials = std::move(materials);
    }

    std::vector<glm::ivec2> ChangedCells(const TerrainHoleMask &before, const TerrainHoleMask &after)
    {
        std::vector<glm::ivec2> cells;
        const int cellsX = std::max(before.GetCellsX(), after.GetCellsX());
        const int cellsZ = std::max(before.GetCellsZ(), after.GetCellsZ());
        for (int z = 0; z < cellsZ; ++z)
            for (int x = 0; x < cellsX; ++x)
                if (before.GetCut(x, z) != after.GetCut(x, z))
                    cells.push_back({x, z});
        return cells;
    }

    std::vector<VolumeChunkCoord> ChunksAlongSurface(const VolumeGrid &grid, const TerrainSurface &surface,
                                                     const std::vector<glm::ivec2> &cells, const TerrainCouplingSettings &settings)
    {
        std::set<VolumeChunkCoord> chunks;
        if (!surface.Valid() || !grid.Valid())
            return {};
        const int ring = std::max(settings.overlapCells, 0);
        // Above and below the surface: the tolerance, the ring's sink and a voxel of meshing slack.
        const float slack = settings.surfaceTolerance * grid.voxelSize + settings.overlapSink + grid.voxelSize;
        std::set<std::pair<int, int>> visited;
        for (const auto &centre : cells)
            for (int dz = -ring; dz <= ring; ++dz)
                for (int dx = -ring; dx <= ring; ++dx)
                {
                    const int x = centre.x + dx, z = centre.y + dz;
                    if (x < 0 || z < 0 || x >= surface.GetCellsX() || z >= surface.GetCellsZ() || !visited.insert({x, z}).second)
                        continue;
                    const glm::vec2 heights = surface.CellHeightRange(x, z);
                    const float cellSize = surface.GetCellSize();
                    auto box = grid.SamplesIn({x * cellSize, heights.x - slack, z * cellSize},
                                              {(x + 1) * cellSize, heights.y + slack, (z + 1) * cellSize});
                    // A cell's triangles come from the edges around it, owned by the chunks either side of its samples.
                    box.first -= 1;
                    box.count += 2;
                    grid.ForEachChunk(box, [&](VolumeChunkCoord chunk) { chunks.insert(chunk); });
                }
        return {chunks.begin(), chunks.end()};
    }
}
