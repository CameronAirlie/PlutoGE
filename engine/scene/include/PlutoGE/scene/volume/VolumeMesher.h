#pragma once

#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/volume/TerrainVolume.h"

#include <glm/glm.hpp>

#include <array>
#include <memory>
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
        // One per vertex, 0..1: how open the surface is around it (1 in the open, lower in crevices, overhangs and,
        // after fitting, deep under the terrain). Empty when the mesher does not compute it (treated as 1).
        std::vector<float> occlusion;
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

    // The dual meshers' shared frame: one vertex per cell the surface crosses, one quad per crossed edge joining the four
    // cells around it, gradient normals, per-vertex material (the deepest corner's) and occlusion. Implementations
    // differ only in where a cell's vertex goes (PlaceVertex), so they share the edge ownership contract, winding and
    // seams. Apron 2: normals read one sample past the cells, which read one past the region.
    class DualMesher : public VolumeMesher
    {
    public:
        // Where the surface crosses a cell's edges: points in cell units (0..1 on each axis) and the surface normals there.
        struct Crossings
        {
            std::array<glm::vec3, 12> points{};
            std::array<glm::vec3, 12> normals{};
            int count = 0;
        };

        [[nodiscard]] int Apron() const override { return 2; }
        [[nodiscard]] VolumeMesh Mesh(const DensityBlock &block, const VolumeSampleBox &region, const VolumeGrid &grid,
                                      const glm::vec3 &origin) const final;
        // Occlusion samples reach this many samples around a vertex (within the apron).
        void SetOcclusionReach(int samples) { m_occlusionReach = samples < 0 ? 0 : (samples > 2 ? 2 : samples); }

    protected:
        // The cell's vertex in cell units; anything outside 0..1 is clamped into the cell.
        [[nodiscard]] virtual glm::vec3 PlaceVertex(const Crossings &crossings) const = 0;

    private:
        int m_occlusionReach = 2;
    };

    // Surface nets: each vertex is the average of its cell's crossings. Smooth, robust, and about half the triangles of
    // marching cubes; sharp rock edges are rounded off.
    class SurfaceNetsMesher final : public DualMesher
    {
    protected:
        [[nodiscard]] glm::vec3 PlaceVertex(const Crossings &crossings) const override;
    };

    // Dual contouring: each vertex minimises its distance to the planes through the crossings (a quadratic error fit,
    // pulled gently towards their average so flat and noisy cells stay put). Keeps the corners and ridges of boxes,
    // cut walls and mined faces that surface nets round off. Same cost class as surface nets.
    class DualContouringMesher final : public DualMesher
    {
    public:
        // How strongly flat or ambiguous cells keep to the crossings' average (0: pure fit; higher: smoother).
        explicit DualContouringMesher(float regularisation = 0.05f) : m_regularisation(regularisation) {}

    protected:
        [[nodiscard]] glm::vec3 PlaceVertex(const Crossings &crossings) const override;

    private:
        float m_regularisation;
    };

    // The meshers a volume can use, by name in settings and files.
    enum class VolumeMesherKind
    {
        SurfaceNets,
        DualContouring,
    };
    [[nodiscard]] std::shared_ptr<const VolumeMesher> MakeVolumeMesher(VolumeMesherKind kind);

    // Gathers what the mesher needs for one chunk of a volume and meshes it, positions relative to the chunk's origin.
    [[nodiscard]] VolumeMesh MeshVolumeChunk(const TerrainVolume &volume, VolumeChunkCoord chunk, const VolumeMesher &mesher);

    // The rock an edit with `shape` would remove from a volume (call it before the edit), as one closed mesh with
    // positions relative to `origin`: the volume's rock inside the shape. Empty when the shape holds no rock. For debris.
    [[nodiscard]] VolumeMesh MeshRemovedRock(const TerrainVolume &volume, const VolumeShape &shape, const VolumeMesher &mesher,
                                             const glm::vec3 &origin);

    // How the terrain layer shader's vertex layers (render::TerrainLayerWeights::VertexLayer) read a vertex:
    //   uv.x  = the triangle's layers a + 256 b + 65536 c (the same on its three vertices, so it never interpolates)
    //   uv.y  = weight of a, uv2.x = weight of b (c takes the rest)
    //   uv2.y = occlusion (1 open)
    // Each vertex weighs its own material fully, so weights blend across a triangle whose corners differ and match
    // along edges shared with neighbouring triangles: layers meet in soft seams.
    namespace VertexLayerEncoding
    {
        [[nodiscard]] float PackLayers(VolumeMaterial a, VolumeMaterial b, VolumeMaterial c);
        [[nodiscard]] std::array<VolumeMaterial, 3> UnpackLayers(float packed);
    }

    // Readies a mesh for the shader's vertex layers (VertexLayerEncoding): each triangle names its corners' materials,
    // and vertices shared by triangles naming different ones are copied. `materials` and `occlusion` follow the vertices.
    void EncodeTriangleMaterials(VolumeMesh &mesh);
}
