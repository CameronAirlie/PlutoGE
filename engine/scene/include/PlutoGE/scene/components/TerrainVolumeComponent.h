#pragma once

#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/scene/volume/TerrainVolume.h"
#include "PlutoGE/scene/volume/TerrainVolumeCoupling.h"
#include "PlutoGE/scene/volume/VolumeMeshQueue.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::render
{
    class Material;
}

namespace PlutoGE::scene
{
    class Entity;
    class TerrainComponent;

    // 3D ground on a terrain (docs/dev/TERRAIN_VOLUMES_PLAN.md): caves, pits, tunnels and mounds the heightfield cannot
    // hold. Beside a TerrainComponent (a lone terrain or a world's tile), it keeps a TerrainVolume whose base is the
    // terrain's surface, applies edits to it, and turns the chunks they change into transient child entities with a
    // mesh and a mesh collider. Where edits move the surface, the terrain's cells are cut (derived holes, never saved
    // with the terrain) and the chunk meshes draw that ground instead.
    //
    // Meshing runs on worker threads, a few chunks a frame; holes change together with the meshes that fill them.
    // Stored chunks persist in the volume file (a world gives each tile its own, beside its height file).
    class TerrainVolumeComponent : public TypedComponent<TerrainVolumeComponent>
    {
    public:
        // The derived holes' source name on the terrain.
        static constexpr std::string_view HoleSource = "TerrainVolume";
        // Every chunk entity carries this tag (and its terrain entity's tags), so gameplay can tell dug ground.
        static constexpr std::string_view ChunkTag = "TerrainVolumeChunk";

        TerrainVolumeComponent();
        ~TerrainVolumeComponent() override;

        void Update(float deltaTime) override;
        void OnRemoved() override;
        void OnEnabledChanged() override;
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        // ---- settings ----------------------------------------------------------------------------------------------
        // The lattice: changing either drops the volume (and its unsaved edits) and reads the file again.
        [[nodiscard]] float GetVoxelSize() const { return m_voxelSize; }
        void SetVoxelSize(float metres);
        [[nodiscard]] int GetChunkVoxels() const { return m_chunkVoxels; }
        void SetChunkVoxels(int samples);
        // The material chunk meshes draw with; empty uses the terrain's.
        [[nodiscard]] const std::string &GetMaterialAsset() const { return m_materialAsset; }
        void SetMaterialAsset(std::string asset);
        // Where the stored chunks are saved (an asset reference or path; a world sets its tiles'). Changing it reads it.
        [[nodiscard]] const std::string &GetVolumeFile() const { return m_volumeFile; }
        void SetVolumeFile(std::string file);
        [[nodiscard]] bool GetCollision() const { return m_collision; }
        void SetCollision(bool collision);
        [[nodiscard]] const volume::TerrainCouplingSettings &GetCoupling() const { return m_coupling; }
        void SetCoupling(const volume::TerrainCouplingSettings &settings);
        // Chunks meshed (and results applied) per frame.
        [[nodiscard]] int GetMeshesPerFrame() const { return m_meshesPerFrame; }
        void SetMeshesPerFrame(int meshes) { m_meshesPerFrame = meshes < 1 ? 1 : meshes; }

        // ---- editing ---------------------------------------------------------------------------------------------
        // Applies a world-space shape. Nothing happens without a terrain beside the component.
        volume::VolumeEditResult Apply(const volume::VolumeShape &worldShape, const volume::VolumeEdit &edit);
        // Applies a shape in the terrain's local space (the volume's own space).
        volume::VolumeEditResult ApplyLocal(const volume::VolumeShape &localShape, const volume::VolumeEdit &edit);
        // The volume (made on first use from the terrain beside the component), or null without one.
        volume::TerrainVolume *GetVolume();
        [[nodiscard]] const volume::TerrainVolume *GetVolume() const { return m_volume.get(); }

        // ---- files -----------------------------------------------------------------------------------------------
        // Writes the stored chunks to the volume file, or removes the file when there are none. False (with a reason)
        // without a file name or when writing fails.
        bool SaveFile(std::string *error = nullptr);
        // The file's contents, already read (a world reads its tiles' on its streaming worker): used instead of reading
        // the volume file. nullopt says there is no file.
        void ProvideFileContents(std::optional<std::vector<std::uint8_t>> contents);
        // The last problem reading the file or editing (empty when none).
        [[nodiscard]] const std::string &GetLastError() const { return m_lastError; }

        // ---- state -----------------------------------------------------------------------------------------------
        // Whether every edit's meshes and holes have landed.
        [[nodiscard]] bool IsUpToDate() const;
        // Meshes everything outstanding now and applies it (loading screens, tools, tests).
        void Flush();
        [[nodiscard]] Entity *GetChunkEntity(volume::VolumeChunkCoord chunk) const;
        [[nodiscard]] std::size_t GetChunkEntityCount() const { return m_chunkEntities.size(); }

    private:
        TerrainComponent *FindTerrain() const;
        // Makes the volume (and reads its file) when there is a terrain; false otherwise.
        bool EnsureVolume();
        void ResetVolume();
        void LoadStoredChunks();
        std::shared_ptr<const volume::TerrainSurface> SnapshotSurface(const TerrainComponent &terrain) const;
        // The steps of an update.
        void FollowTerrainHeights(TerrainComponent &terrain);
        void RefreshHoles();
        void ScheduleMeshing(std::size_t budget);
        void ApplyMeshes(std::vector<volume::VolumeMeshQueue::Result> results);
        void ApplyHolesWhenMeshed(TerrainComponent &terrain);
        void Step(bool unbudgeted);
        void ValidateChunkEntities();
        void DestroyChunkEntities();
        void InvalidateNavigation(const glm::vec3 &localMin, const glm::vec3 &localMax) const;
        render::Material *ChunkMaterial() const;

        // Settings.
        float m_voxelSize = 0.5f;
        int m_chunkVoxels = 32;
        std::string m_materialAsset;
        std::string m_volumeFile;
        bool m_collision = true;
        volume::TerrainCouplingSettings m_coupling;
        int m_meshesPerFrame = 4;

        // The volume and what it is built from.
        std::unique_ptr<volume::TerrainVolume> m_volume;
        std::shared_ptr<const volume::TerrainSurface> m_surface;
        std::uint64_t m_surfaceHeightsRevision = 0;
        std::optional<std::optional<std::vector<std::uint8_t>>> m_providedFile; // set: use it instead of reading
        bool m_fileLoaded = false;
        std::string m_lastError;

        // Holes: those the terrain has, and those the latest edits call for (applied once their meshes are in).
        std::shared_ptr<const TerrainHoleMask> m_holes;
        std::shared_ptr<const TerrainHoleMask> m_pendingHoles;
        std::uint64_t m_holesVolumeRevision = 0;

        // Meshing.
        std::unique_ptr<volume::VolumeMeshQueue> m_queue;
        std::set<volume::VolumeChunkCoord> m_toMesh;
        std::map<volume::VolumeChunkCoord, std::uint32_t> m_chunkEntities; // entity IDs
    };
}
