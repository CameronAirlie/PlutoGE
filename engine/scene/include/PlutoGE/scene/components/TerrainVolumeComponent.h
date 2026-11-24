#pragma once

#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/scene/components/VolumeStampComponent.h"
#include "PlutoGE/scene/volume/CaveDensity.h"
#include "PlutoGE/scene/volume/TerrainVolume.h"
#include "PlutoGE/scene/volume/VolumeFarProxy.h"
#include "PlutoGE/scene/volume/TerrainVolumeCoupling.h"
#include "PlutoGE/scene/volume/VolumeMeshQueue.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
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
    //
    // The volume's base is the terrain's ground (its painted layer at the surface, an optional rock layer deeper down)
    // with the caves of a ProceduralTerrainComponent beside it carved out (its definition's `caves`), then the scene's
    // VolumeStampComponents over the terrain applied. The base follows sculpting, painting, the definition and the
    // stamps; generated caves and stamps are meshed without being stored.
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
        // The material chunk meshes draw with; empty uses the terrain's layers, triplanar, each triangle in the layer
        // its rock has (a terrain without layers lends its plain material).
        [[nodiscard]] const std::string &GetMaterialAsset() const { return m_materialAsset; }
        void SetMaterialAsset(std::string asset);
        // Where the stored chunks are saved (an asset reference or path; a world sets its tiles'). Changing it reads it.
        [[nodiscard]] const std::string &GetVolumeFile() const { return m_volumeFile; }
        void SetVolumeFile(std::string file);
        [[nodiscard]] bool GetCollision() const { return m_collision; }
        void SetCollision(bool collision);
        [[nodiscard]] const volume::TerrainCouplingSettings &GetCoupling() const { return m_coupling; }
        void SetCoupling(const volume::TerrainCouplingSettings &settings);
        // How chunks are meshed: smooth surface nets, or dual contouring that keeps sharp rock edges. Changing it
        // meshes every chunk again.
        [[nodiscard]] volume::VolumeMesherKind GetMesher() const { return m_mesherKind; }
        void SetMesher(volume::VolumeMesherKind kind);
        // Crevice and cave darkness (occlusion carried by the chunk vertices).
        [[nodiscard]] const volume::VolumeShadingSettings &GetShading() const { return m_shading; }
        void SetShading(const volume::VolumeShadingSettings &shading);
        // What dug ground is made of below the surface: strata of terrain layers by depth (the base; stored chunks keep
        // what they had). Saved as "Strata" ("layer@depth;..."); scenes from before strata load RockLayer/RockDepth.
        [[nodiscard]] const volume::TerrainGroundMaterials &GetGroundMaterials() const { return m_ground; }
        void SetGroundMaterials(const volume::TerrainGroundMaterials &materials);
        // Chunks meshed (and results applied) per frame.
        [[nodiscard]] int GetMeshesPerFrame() const { return m_meshesPerFrame; }
        void SetMeshesPerFrame(int meshes) { m_meshesPerFrame = meshes < 1 ? 1 : meshes; }

        // ---- editing ---------------------------------------------------------------------------------------------
        // Applies a world-space shape. Nothing happens without a terrain beside the component.
        volume::VolumeEditResult Apply(const volume::VolumeShape &worldShape, const volume::VolumeEdit &edit);
        // Applies a shape in the terrain's local space (the volume's own space).
        volume::VolumeEditResult ApplyLocal(const volume::VolumeShape &localShape, const volume::VolumeEdit &edit);
        // The rock an edit with this world-space shape would remove (call before the edit), as a closed mesh in the
        // volume's space with positions relative to `localOrigin`. For debris (SpawnTerrainDebris).
        [[nodiscard]] volume::VolumeMesh MeshRemovedRock(const volume::VolumeShape &worldShape, glm::vec3 &localOrigin);
        // What the chunk entities draw with (the layered terrain material, a material asset, or the terrain's own).
        [[nodiscard]] render::Material *GetChunkMaterial() { return ChunkMaterial(); }
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
        // The stored chunks as a volume file's bytes (save games, a world keeping a streamed-out tile's edits).
        [[nodiscard]] std::vector<std::uint8_t> ExportEdits();
        // Replaces every stored chunk with those of a volume file's bytes, as if the volume had been loaded from them.
        // False (with a reason, and nothing changed) when they are damaged or were written for another grid.
        bool ImportEdits(std::span<const std::uint8_t> bytes, std::string *error = nullptr);
        // Replaces the stored chunks with the volume file's (none when there is no file): drops unsaved edits.
        bool ReloadFile(std::string *error = nullptr);
        // Whether the stored chunks changed since they were loaded, imported or saved.
        [[nodiscard]] bool HasUnsavedEdits() const;
        // The last problem reading the file or editing (empty when none).
        [[nodiscard]] const std::string &GetLastError() const { return m_lastError; }

        // ---- state -----------------------------------------------------------------------------------------------
        // Whether every edit's meshes and holes have landed.
        [[nodiscard]] bool IsUpToDate() const;
        // Meshes everything outstanding now and applies it (loading screens, tools, tests).
        void Flush();
        [[nodiscard]] Entity *GetChunkEntity(volume::VolumeChunkCoord chunk) const;
        [[nodiscard]] std::size_t GetChunkEntityCount() const { return m_chunkEntities.size(); }

        // ---- distance (phase 9) --------------------------------------------------------------------------------------
        // The volume as coarse ground over a coarser drawing of the same terrain (a world's overview of this tile): the
        // cells it cuts there and the meshes that fill them, with voxels `step` times this volume's. Empty when there
        // is nothing but plain ground. Built on the calling thread; PrepareFarProxy builds it elsewhere.
        [[nodiscard]] volume::VolumeFarProxy BuildFarProxy(std::shared_ptr<const volume::TerrainSurface> farSurface, int step);
        // What building a far proxy reads, copied (the volume's stored chunks, the far base, the mesher), so a tile that
        // streams out can build its proxy on a worker thread: Build() may run on any thread.
        struct FarProxyJob
        {
            volume::TerrainVolume volume;
            std::shared_ptr<const volume::VolumeDensitySource> base;
            std::shared_ptr<const volume::TerrainSurface> surface;
            std::shared_ptr<const volume::VolumeMesher> mesher;
            volume::VolumeFarProxySettings settings;
            [[nodiscard]] volume::VolumeFarProxy Build() const;
        };
        // Nothing when there is nothing but plain ground (the proxy would be empty).
        [[nodiscard]] std::optional<FarProxyJob> PrepareFarProxy(std::shared_ptr<const volume::TerrainSurface> farSurface, int step);
        // A terrain's drawn surface as the volume sees it (heights and strongest layers).
        [[nodiscard]] static std::shared_ptr<const volume::TerrainSurface> SnapshotSurface(const TerrainComponent &terrain);

    private:
        TerrainComponent *FindTerrain() const;
        // Makes the volume (and reads its file) when there is a terrain; false otherwise.
        bool EnsureVolume();
        void ResetVolume();
        void LoadStoredChunks();

        // A stamp as the base applies it.
        struct StampInput
        {
            VolumeStampComponent::Shape shape = VolumeStampComponent::Shape::Sphere;
            volume::VolumeEditMode mode = volume::VolumeEditMode::Subtract;
            int layer = 0;
            float radius = 0.0f;
            glm::vec3 halfExtents{0.0f};
            float length = 0.0f;
            glm::mat4 toStamp{1.0f}; // the volume's space to the stamp's
            bool operator==(const StampInput &) const = default;
        };
        // Everything the base is made from; a change rebuilds it.
        struct BaseInputs
        {
            std::uint64_t heightsRevision = 0;
            std::uint64_t weightsRevision = 0;
            volume::TerrainGroundMaterials ground;
            volume::CaveNetworkSettings caves;
            std::uint32_t caveSeed = 0;
            glm::vec3 worldOffset{0.0f};
            std::vector<StampInput> stamps;
            bool operator==(const BaseInputs &) const = default;
        };
        [[nodiscard]] BaseInputs CurrentBaseInputs(const TerrainComponent &terrain) const;
        // The base for these inputs: the ground, with the caves carved out. Also snapshots the surface.
        std::shared_ptr<const volume::VolumeDensitySource> MakeBase(const TerrainComponent &terrain, const BaseInputs &inputs);
        // The same base over another surface (a coarse one, for far proxies).
        static std::shared_ptr<const volume::VolumeDensitySource> MakeBaseOver(const std::shared_ptr<const volume::TerrainSurface> &surface,
                                                                              const BaseInputs &inputs);
        // Queues the meshes of the base's features (generated caves).
        void QueueBaseFeatures();
        // The steps of an update.
        void FollowBase(TerrainComponent &terrain, bool immediately);
        void RefreshHoles();
        void ScheduleMeshing(std::size_t budget);
        void ApplyMeshes(std::vector<volume::VolumeMeshQueue::Result> results);
        void ApplyHolesWhenMeshed(TerrainComponent &terrain);
        void Step(bool unbudgeted);
        void ValidateChunkEntities();
        void DestroyChunkEntities();
        void InvalidateNavigation(const glm::vec3 &localMin, const glm::vec3 &localMax) const;
        render::Material *ChunkMaterial();
        // Follows the terrain's layers and material into the chunks' derived material; true when it changed.
        void RefreshChunkMaterial();
        render::Material *LayeredTerrainMaterial();

        // Settings.
        float m_voxelSize = 0.5f;
        int m_chunkVoxels = 32;
        std::string m_materialAsset;
        std::string m_volumeFile;
        bool m_collision = true;
        volume::TerrainCouplingSettings m_coupling;
        volume::TerrainGroundMaterials m_ground;
        volume::VolumeMesherKind m_mesherKind = volume::VolumeMesherKind::SurfaceNets;
        volume::VolumeShadingSettings m_shading;
        int m_meshesPerFrame = 4;

        // The volume and what it is built from.
        std::unique_ptr<volume::TerrainVolume> m_volume;
        std::shared_ptr<const volume::TerrainSurface> m_surface;
        BaseInputs m_baseInputs;
        std::uint64_t m_weightsSeen = 0; // painting settles for a frame before the base follows it
        std::optional<std::optional<std::vector<std::uint8_t>>> m_providedFile; // set: use it instead of reading
        bool m_fileLoaded = false;
        std::uint64_t m_savedRevision = 0; // the volume's revision when its chunks matched what was loaded or saved
        std::string m_lastError;

        // Holes: those the terrain has, and those the latest edits call for (applied once their meshes are in).
        std::shared_ptr<const TerrainHoleMask> m_holes;
        std::shared_ptr<const TerrainHoleMask> m_pendingHoles;
        std::uint64_t m_holesVolumeRevision = 0;

        // Meshing.
        std::unique_ptr<volume::VolumeMeshQueue> m_queue;
        std::set<volume::VolumeChunkCoord> m_toMesh;
        std::map<volume::VolumeChunkCoord, std::uint32_t> m_chunkEntities; // entity IDs

        // The terrain's layered material, re-projected for chunks (vertex layers, triplanar).
        std::unique_ptr<render::Material> m_layeredMaterial;
        const render::Material *m_layeredSource = nullptr;
        std::uint64_t m_layeredSourceRevision = 0;
        render::Material *m_appliedMaterial = nullptr; // what the chunk entities draw with
    };
}
