#pragma once

#include "PlutoGE/scene/TerrainHoleMask.h"
#include "PlutoGE/scene/volume/TerrainVolume.h"
#include "PlutoGE/scene/volume/VolumeMesher.h"

#include <glm/glm.hpp>

#include <memory>
#include <optional>
#include <vector>

namespace PlutoGE::scene::volume
{
    // How a terrain volume and its heightfield share the ground (docs/dev/TERRAIN_VOLUMES_PLAN.md, "Coupling"):
    //
    // - The volume's base is the terrain's drawn surface, so unedited, the two agree.
    // - Where edits move that surface (a pit, a cave mouth, a mound), the terrain's cells are cut (derived holes) and
    //   the volume draws the ground instead.
    // - A volume mesh keeps what the heightfield cannot draw: everything over cut cells, everything away from the
    //   surface (cave interiors, overhangs), and a ring of surface over the uncut cells around the cut ones. The ring
    //   sinks a little so the terrain wins the depth test where both draw, while it fills the cracks left between a
    //   cell grid and a voxel surface.
    //
    // All of it works in the terrain's local space (the volume's space) and is pure: no scene, no renderer.

    // The terrain's surface as it draws at full detail: smoothed sample heights, each cell split along its
    // (x + 1, z) - (x, z + 1) diagonal. An immutable snapshot, safe to share with meshing workers.
    class TerrainSurface
    {
    public:
        TerrainSurface() = default;
        // `heights` holds width x depth samples, x fastest, as the terrain draws them (GetSurfaceSampleHeight).
        TerrainSurface(int width, int depth, float cellSize, std::vector<float> heights);

        [[nodiscard]] bool Valid() const;
        [[nodiscard]] int GetCellsX() const { return m_width - 1; }
        [[nodiscard]] int GetCellsZ() const { return m_depth - 1; }
        [[nodiscard]] float GetCellSize() const { return m_cellSize; }
        // The surface's height at a local position; nullopt off the terrain.
        [[nodiscard]] std::optional<float> Height(float x, float z) const;
        // The lowest and highest corner heights of a cell.
        [[nodiscard]] glm::vec2 CellHeightRange(int cellX, int cellZ) const;

    private:
        [[nodiscard]] float Sample(int x, int z) const { return m_heights[static_cast<std::size_t>(z) * m_width + x]; }

        int m_width = 0;
        int m_depth = 0;
        float m_cellSize = 1.0f;
        std::vector<float> m_heights;
    };

    // A volume base that is the terrain's surface (HeightfieldDensity over the snapshot).
    [[nodiscard]] std::shared_ptr<const VolumeDensitySource> MakeTerrainDensity(std::shared_ptr<const TerrainSurface> surface,
                                                                                VolumeMaterial material = 0);

    struct TerrainCouplingSettings
    {
        // A column whose samples at the terrain's surface moved more than this many voxels from the base cuts the cells
        // around it: the volume, not the heightfield, has the ground there.
        float breachTolerance = 0.25f;
        // A volume triangle whose corners all lie within this many voxels of the surface, over uncut cells, is the
        // heightfield's ground again and is left to the heightfield (outside the overlap ring).
        float surfaceTolerance = 0.5f;
        // Uncut cells around the cut ones where the volume still draws the surface (0: none).
        int overlapCells = 1;
        // How far (metres) the ring's surface sinks below the terrain, reached a voxel away from the cut cells.
        float overlapSink = 0.05f;
        bool operator==(const TerrainCouplingSettings &) const = default;
    };

    // The cells where a volume's stored chunks have moved the terrain's surface: whole-cell cuts in a mask of the
    // surface's cells. Columns of unstored chunks are never breached (they read the base).
    [[nodiscard]] TerrainHoleMask ComputeVolumeHoles(const TerrainVolume &volume, const TerrainSurface &surface,
                                                     const TerrainCouplingSettings &settings);

    // Trims a chunk's mesh (positions relative to `meshOrigin`, the chunk's origin in the terrain's space) to what the
    // heightfield cannot draw given the volume's holes, sinks the overlap ring, and drops the vertices no longer used.
    void FitVolumeMeshToTerrain(VolumeMesh &mesh, const glm::vec3 &meshOrigin, const TerrainSurface &surface,
                                const TerrainHoleMask &volumeHoles, const TerrainCouplingSettings &settings, float voxelSize);

    // The cells whose cut differs between two masks of the same size.
    [[nodiscard]] std::vector<glm::ivec2> ChangedCells(const TerrainHoleMask &before, const TerrainHoleMask &after);

    // The chunks whose fitted meshes depend on these cells' cuts: those holding the surface over the cells and their
    // overlap ring. After holes change, they mesh again (a newly cut cell needs its neighbours' ring).
    [[nodiscard]] std::vector<VolumeChunkCoord> ChunksAlongSurface(const VolumeGrid &grid, const TerrainSurface &surface,
                                                                   const std::vector<glm::ivec2> &cells,
                                                                   const TerrainCouplingSettings &settings);
}
