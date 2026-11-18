#pragma once

#include "PlutoGE/scene/volume/VolumeChunk.h"
#include "PlutoGE/scene/volume/VolumeDensitySource.h"
#include "PlutoGE/scene/volume/VolumeGrid.h"
#include "PlutoGE/scene/volume/VolumeShape.h"

#include <cstdint>
#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

namespace PlutoGE::scene::volume
{
    enum class VolumeEditMode
    {
        Subtract, // the shape becomes air: digging, tunnels
        Add,      // the shape becomes rock of the edit's material: filling, building up
        Paint,    // rock inside the shape takes the edit's material; the surface does not move
    };

    struct VolumeEdit
    {
        VolumeEditMode mode = VolumeEditMode::Subtract;
        VolumeMaterial material = 0; // Add and Paint
    };

    struct VolumeEditResult
    {
        // The chunks whose stored samples changed (now stored, whether or not they were before).
        std::vector<VolumeChunkCoord> changedChunks;
        // The samples that changed (empty when nothing did).
        VolumeSampleBox changedSamples;
        [[nodiscard]] bool Changed() const { return !changedChunks.empty(); }
    };

    // A box of samples with their distances in metres, as meshing reads them.
    struct DensityBlock
    {
        VolumeSampleBox box;
        std::vector<VolumeSample> samples; // x fastest, then y, then z
        [[nodiscard]] std::size_t Index(glm::ivec3 local) const
        {
            return (static_cast<std::size_t>(local.z) * box.count.y + static_cast<std::size_t>(local.y)) * box.count.x +
                   static_cast<std::size_t>(local.x);
        }
        // Local coordinates: 0 .. box.count - 1.
        [[nodiscard]] const VolumeSample &At(glm::ivec3 local) const { return samples[Index(local)]; }
    };

    // A terrain's 3D ground: a base density (the heightfield, generated caves) everywhere, and stored chunks wherever
    // something edited it. Unedited chunks are never stored, so a volume over a whole world costs only what was dug.
    //
    // Pure data with no scene or renderer dependency (docs/dev/TERRAIN_VOLUMES_PLAN.md). Not thread-safe: edit and
    // gather on one thread, then hand the gathered DensityBlock to a meshing worker.
    class TerrainVolume
    {
    public:
        // How far past a chunk its mesh reads samples (normals look one sample further than the surface does). Edits
        // dirty every chunk whose mesh read a changed sample. Matches SurfaceNetsMesher; set it for another mesher.
        static constexpr int DefaultMeshApron = 2;

        TerrainVolume(VolumeGrid grid, std::shared_ptr<const VolumeDensitySource> base);

        [[nodiscard]] const VolumeGrid &GetGrid() const { return m_grid; }
        [[nodiscard]] const VolumeDensitySource &GetBase() const { return *m_base; }
        [[nodiscard]] int GetMeshApron() const { return m_meshApron; }
        void SetMeshApron(int samples) { m_meshApron = samples < 0 ? 0 : samples; }

        // A sample: the stored chunk's, else the base's, quantized as stored samples are, so a chunk reads the same
        // before and after it is first stored.
        [[nodiscard]] VolumeSample Sample(glm::ivec3 sample) const;
        // Every sample of a box (stored chunks where there are some, the base elsewhere), for meshing.
        [[nodiscard]] DensityBlock Gather(const VolumeSampleBox &box) const;

        // ---- edits ------------------------------------------------------------------------------------------------
        // Applies a shape to the volume. Chunks it changes are stored; chunks it leaves as they were are not. Dirties
        // every chunk whose mesh read a changed sample.
        VolumeEditResult Apply(const VolumeShape &shape, const VolumeEdit &edit);

        // ---- stored chunks (files, save games, undo) -----------------------------------------------------------------
        [[nodiscard]] const VolumeChunk *FindChunk(VolumeChunkCoord chunk) const;
        [[nodiscard]] std::size_t GetStoredChunkCount() const { return m_chunks.size(); }
        // In coordinate order, so files written from them are stable.
        [[nodiscard]] std::vector<VolumeChunkCoord> GetStoredChunks() const;
        // Stores a chunk as given (loading, undo). False when its size is not the grid's chunk size.
        bool SetChunk(VolumeChunkCoord chunk, VolumeChunk samples);
        // Drops a stored chunk: it reads as the base again. False when it was not stored.
        bool RemoveChunk(VolumeChunkCoord chunk);
        // The base's samples for a chunk, as storing it would hold them.
        [[nodiscard]] VolumeChunk MakeBaseChunk(VolumeChunkCoord chunk) const;

        // ---- change tracking --------------------------------------------------------------------------------------
        // Chunks whose mesh may differ from when TakeDirtyChunks last ran.
        [[nodiscard]] const std::set<VolumeChunkCoord> &GetDirtyChunks() const { return m_dirty; }
        std::vector<VolumeChunkCoord> TakeDirtyChunks();
        // Changes with every change to the stored samples.
        [[nodiscard]] std::uint64_t GetRevision() const { return m_revision; }

    private:
        void MarkDirty(const VolumeSampleBox &changedSamples);

        VolumeGrid m_grid;
        std::shared_ptr<const VolumeDensitySource> m_base;
        int m_meshApron = DefaultMeshApron;
        std::unordered_map<VolumeChunkCoord, VolumeChunk, VolumeChunkCoordHash> m_chunks;
        std::set<VolumeChunkCoord> m_dirty;
        std::uint64_t m_revision = 0;
    };
}
