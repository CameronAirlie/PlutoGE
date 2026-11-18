#pragma once

#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/volume/TerrainVolume.h"

#include <glm/glm.hpp>

#include <vector>

namespace PlutoGE::scene::volume
{
    // A surface extracted from a volume.
    struct VolumeMesh
    {
        // Triangles wound counter-clockwise seen from the air side; normals point into the air. UVs are zero (volume
        // materials project their textures), tangents follow the normal.
        render::MeshData mesh;
        // One per vertex: the material of the rock it sits on (VolumeMaterial, the terrain's layer index).
        std::vector<VolumeMaterial> materials;
        glm::vec3 boundsMin{0.0f};
        glm::vec3 boundsMax{0.0f};
        [[nodiscard]] bool Empty() const { return mesh.indices.empty(); }
    };

    // Turns density samples into triangles. Implementations differ in the surface they make (smooth or sharp, with or
    // without LOD) but share this contract, so callers and containers never depend on one of them:
    //
    // - A mesh owns the surface crossing the lattice edges that **start** at the samples of its region (the edge from
    //   sample s to s + 1 along each axis). Neighbouring regions own disjoint edges, so meshing chunks one at a time
    //   gives one surface without gaps or overlaps.
    // - It reads the samples of RequiredSamples(region): the region plus Apron() samples on every side.
    // - It is deterministic and has no state, so one mesher can serve several worker threads.
    class VolumeMesher
    {
    public:
        virtual ~VolumeMesher() = default;
        // Samples needed beyond the region on every side.
        [[nodiscard]] virtual int Apron() const = 0;
        // Meshes `region` from `block` (which must hold RequiredSamples(region)); positions are in the volume's space
        // minus `origin`. Returns an empty mesh when the block does not hold what it needs.
        [[nodiscard]] virtual VolumeMesh Mesh(const DensityBlock &block, const VolumeSampleBox &region, const VolumeGrid &grid,
                                              const glm::vec3 &origin) const = 0;

        [[nodiscard]] VolumeSampleBox RequiredSamples(const VolumeSampleBox &region) const
        {
            return {region.first - Apron(), region.count + 2 * Apron()};
        }
    };

    // Surface nets: one vertex per cell the surface crosses (the average of its edge crossings), one quad per crossed
    // edge. Smooth, robust, and about half the triangles of marching cubes; sharp rock edges are rounded off (a dual
    // contouring mesher would keep them).
    class SurfaceNetsMesher final : public VolumeMesher
    {
    public:
        [[nodiscard]] int Apron() const override { return 2; }
        [[nodiscard]] VolumeMesh Mesh(const DensityBlock &block, const VolumeSampleBox &region, const VolumeGrid &grid,
                                      const glm::vec3 &origin) const override;
    };

    // Gathers what the mesher needs for one chunk of a volume and meshes it, positions relative to the chunk's origin.
    [[nodiscard]] VolumeMesh MeshVolumeChunk(const TerrainVolume &volume, VolumeChunkCoord chunk, const VolumeMesher &mesher);
}
