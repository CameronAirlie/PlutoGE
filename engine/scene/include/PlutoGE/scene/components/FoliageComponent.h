#pragma once

#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/RenderCommand.h"
#include "PlutoGE/scene/FoliageTypeAsset.h"
#include "PlutoGE/scene/HeavyPropertyCache.h"
#include "PlutoGE/scene/components/Component.h"

#include <glm/glm.hpp>
#include <functional>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace PlutoGE::render
{
    class Material;
}

namespace PlutoGE::scene
{
    struct FoliageInstance
    {
        std::uint64_t id = 0;
        glm::vec3 position{0.0f};
        glm::vec3 rotationDegrees{0.0f};
        glm::vec3 scale{1.0f};
        bool operator==(const FoliageInstance &) const = default;
    };

    using FoliageInstanceSnapshot = std::vector<std::vector<FoliageInstance>>;

    struct FoliageCellCoordinate
    {
        int x = 0;
        int z = 0;
        bool operator==(const FoliageCellCoordinate &) const = default;
    };

    struct FoliageCellCoordinateHash
    {
        std::size_t operator()(const FoliageCellCoordinate &coordinate) const noexcept;
    };

    struct FoliageCollisionInstance
    {
        std::uint64_t instanceId = 0;
        glm::mat4 worldTransform{1.0f};
        glm::vec3 center{0.0f};
        float radius = 0.35f;
        float height = 2.0f;
    };

    struct FoliageCollisionCell
    {
        FoliageCellCoordinate coordinate;
        std::vector<FoliageCollisionInstance> instances;
    };

    struct FoliageType
    {
        std::string name = "Foliage";
        render::Mesh *mesh = nullptr;
        render::Material *materialOverride = nullptr;
        std::vector<render::Material *> materials;
        std::vector<std::unique_ptr<render::Material>> ownedMaterials;
        // The mesh asset's baked impostor, when its import made one; distant instances draw as its cards.
        std::shared_ptr<const render::MeshImpostor> impostor;
        std::vector<FoliageInstance> instances;
        // All authored settings. With `asset.assetReference` set they come from that file, and the scene's copy is
        // only a fallback.
        FoliageTypeAsset asset;
        // Why the linked file could not be read, when it could not; the type then uses the scene's copy.
        std::string assetLoadError;
        // `instances` as scene text, valid for the owning component's heavy data revision.
        HeavyPropertyCache instancesText;
    };

    // Where an instance currently is. Indices change as instances are added and removed: keep an instance's ID, and
    // resolve it with FoliageComponent::FindInstance.
    struct FoliageInstanceLocation
    {
        std::size_t typeIndex = 0;
        std::size_t instanceIndex = 0;
        bool operator==(const FoliageInstanceLocation &) const = default;
    };

    struct FoliageRemovedInstance
    {
        FoliageInstanceLocation location;
        FoliageInstance instance;
    };

    struct FoliageInstanceHit
    {
        std::uint64_t instanceId = 0;
        FoliageInstanceLocation location;
        float distance = 0.0f;
    };

    struct FoliageInstanceBounds
    {
        // Maps the local box to the world, as the instance draws.
        glm::mat4 model{1.0f};
        glm::vec3 localMin{0.0f};
        glm::vec3 localMax{0.0f};
        glm::vec3 worldCenter{0.0f};
        float worldRadius = 0.0f;
    };

    // A world-space edit of placed instances: rotate and scale about `pivot`, then translate.
    struct FoliageInstanceWorldEdit
    {
        glm::vec3 pivot{0.0f};
        glm::mat3 rotation{1.0f};
        glm::vec3 scale{1.0f};
        glm::vec3 translation{0.0f};
    };

    // `instance` after `edit`, for an instance owned by an entity with this world transform. Instance scale applies
    // along the instance's own axes, so a non-uniform edit is only exact for one instance whose axes it was made in.
    [[nodiscard]] FoliageInstance ApplyFoliageInstanceWorldEdit(const glm::mat4 &ownerTransform,
                                                                const FoliageInstance &instance,
                                                                const FoliageInstanceWorldEdit &edit);

    // The prefab override list after removing type `removedIndex`: its `Type.N.*` entries are dropped and those of
    // later types move down one, so they keep naming the same types.
    [[nodiscard]] std::vector<std::string> RemapFoliageOverridesAfterTypeRemoval(const std::vector<std::string> &overrides,
                                                                                 std::size_t removedIndex);

    struct FoliageSurfaceSample
    {
        float height = 0.0f;
        glm::vec3 normal{0.0f, 1.0f, 0.0f}; // In foliage owner local space.
    };

    using FoliageSurfaceSampler = std::function<std::optional<FoliageSurfaceSample>(float, float)>;

    enum class FoliageBrushMode
    {
        Add,
        Remove,
    };

    enum class FoliageScaleMode
    {
        Multiply,  // Keeps each instance's variation and proportions.
        Set,       // Every instance gets the same uniform scale.
        Randomize, // Uniform scale in [minScale, maxScale], stable per instance.
    };

    // One brush application's settings. Brushes are an authoring tool: the editor owns these, not the scene.
    struct FoliageBrush
    {
        FoliageBrushMode mode = FoliageBrushMode::Add;
        float radius = 5.0f;
        int density = 3;
    };

    struct FoliageScaleEdit
    {
        FoliageScaleMode mode = FoliageScaleMode::Multiply;
        float factor = 1.0f;
        float scale = 1.0f;
        float minScale = 0.8f;
        float maxScale = 1.2f;
    };

    class FoliageComponent : public TypedComponent<FoliageComponent>
    {
    public:
        FoliageComponent() = default;
        ~FoliageComponent() override = default;

        void Update(float deltaTime) override;
        void SubmitRenderCommands();
        // The instanced draws SubmitRenderCommands submits (one per type, cell and submesh), rebuilt when stale. For
        // tools that render a scene outside the engine's renderer and must draw foliage as the runtime does.
        const std::vector<render::RenderCommand> &GetRenderCommands();

        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;
        std::vector<Property> SerializeForInspector() const override;
        std::vector<Property> SerializeShared() const override;
        void DeserializeKeepingHeavyData(const std::vector<Property> &properties) override;
        std::uint64_t GetHeavyDataRevision() const override { return m_instanceTextRevision; }
        bool IsRetiredProperty(std::string_view name) const override;

        void SetMesh(render::Mesh *mesh);
        render::Mesh *GetMesh() const;
        void SetMaterial(render::Material *material) { m_material = material; }
        render::Material *GetMaterial() const { return m_material; }
        void SetSourceMeshPath(const std::string &sourceMeshPath);
        const std::string &GetSourceMeshPath() const;
        void SetMaterialAssetReference(const std::string &materialAssetReference);
        const std::string &GetMaterialAssetReference() const;

        void SetMaxDrawDistance(float distance);
        float GetMaxDrawDistance() const { return m_maxDrawDistance; }
        void SetMaxShadowDistance(float distance);
        float GetMaxShadowDistance() const { return m_maxShadowDistance; }
        void SetMinRenderLod(int lodIndex);
        int GetMinRenderLod() const { return m_minRenderLod; }
        void SetMinShadowLod(int lodIndex);
        int GetMinShadowLod() const { return m_minShadowLod; }
        void SetCastShadows(bool castShadows);
        bool GetCastShadows() const { return m_castShadows; }
        void SetStatic(bool isStatic)
        {
            if (m_isStatic == isStatic)
                return;
            m_isStatic = isStatic;
            MarkRenderCommandsDirty();
        }
        bool IsStatic() const { return m_isStatic; }
        std::vector<std::size_t> GetBakeSubmeshes(std::size_t typeIndex) const;
        render::Material *GetBakeMaterial(std::size_t typeIndex, std::size_t submeshIndex) const;
        glm::mat4 GetBakeInstanceTransform(std::size_t typeIndex, std::size_t instanceIndex) const;

        // Paints or erases the selected type around `worldPosition`. A missing sample rejects the candidate (bounds,
        // holes, or filters). New instances take the type's scale range and growth orientation.
        bool ApplyBrush(const FoliageBrush &brush, const glm::vec3 &worldPosition,
                        const FoliageSurfaceSampler &sampleSurface);

        void ClearInstances();
        void ClearSelectedTypeInstances();
        const std::vector<FoliageInstance> &GetInstances() const;
        FoliageInstance *GetSelectedTypeInstance(std::size_t instanceIndex);
        const FoliageInstance *GetSelectedTypeInstance(std::size_t instanceIndex) const;
        bool SetSelectedTypeInstanceTransform(std::size_t instanceIndex,
                                              const glm::vec3 &position,
                                              const glm::vec3 &rotationDegrees,
                                              const glm::vec3 &scale);
        std::size_t SetSelectedTypeInstancesScale(const glm::vec3 &scale);
        // Rescales placed instances of one type, or of every type when `typeIndex` is empty, around their own pivots
        // (positions stay). Returns how many instances changed.
        std::size_t ApplyInstanceScaleEdit(const FoliageScaleEdit &edit,
                                           std::optional<std::size_t> typeIndex = std::nullopt);
        std::size_t SnapSelectedTypeInstancesToSurface(const std::function<float(float, float)> &sampleTerrainHeight);
        std::size_t SnapAllInstancesToSurface(const std::function<float(float, float)> &sampleTerrainHeight);
        bool RemoveSelectedTypeInstance(std::size_t instanceIndex);

        // Individual instances, by the ID that stays valid while others change. IDs are unique within a component.
        // `hint`, usually where the instance last was, avoids searching when it is still there.
        [[nodiscard]] std::optional<FoliageInstanceLocation> FindInstance(
            std::uint64_t instanceId, std::optional<FoliageInstanceLocation> hint = std::nullopt) const;
        [[nodiscard]] const FoliageInstance *GetInstance(const FoliageInstanceLocation &location) const;
        // Replaces the instances with these IDs. Returns how many changed; unknown IDs are skipped.
        std::size_t SetInstances(std::span<const FoliageInstance> instances);
        // Removes these instances, returning them in the order RestoreInstances needs.
        std::vector<FoliageRemovedInstance> RemoveInstances(std::span<const std::uint64_t> instanceIds);
        void RestoreInstances(std::span<const FoliageRemovedInstance> removed);
        // The nearest instance along a world-space ray with a normalized direction. Tests triangles where the mesh
        // keeps them; a ray through the gaps of a canopy still takes the instance it passes closest to the middle of.
        [[nodiscard]] std::optional<FoliageInstanceHit> PickInstance(const glm::vec3 &origin, const glm::vec3 &direction) const;
        [[nodiscard]] std::optional<FoliageInstanceBounds> GetInstanceBounds(const FoliageInstanceLocation &location) const;
        // The instance's pivot and rotation in the world, without its scale: what a transform gizmo manipulates.
        [[nodiscard]] std::optional<glm::mat4> GetInstancePivotTransform(const FoliageInstanceLocation &location) const;
        std::size_t GetTotalInstanceCount() const;
        std::size_t GetSelectedTypeInstanceCount() const;
        FoliageInstanceSnapshot CaptureInstanceSnapshot() const;
        void RestoreInstanceSnapshot(const FoliageInstanceSnapshot &snapshot);
        const std::vector<FoliageCollisionCell> &BuildCollisionCells() const;
        std::uint64_t GetRevision() const { return m_revision; }

        std::size_t GetTypeCount() const { return m_types.size(); }
        int GetSelectedTypeIndex() const { return m_selectedTypeIndex; }
        void SetSelectedTypeIndex(int index);
        FoliageType *GetType(std::size_t index);
        const FoliageType *GetType(std::size_t index) const;
        FoliageType *GetSelectedType();
        const FoliageType *GetSelectedType() const;
        FoliageType &AddType(std::string name = {});
        void RemoveType(std::size_t index);
        void SetTypeName(std::size_t index, const std::string &name);
        void SetTypeSubmeshIndex(std::size_t index, int submeshIndex);
        void SetTypeSubmeshIndices(std::size_t index, const std::vector<int> &submeshIndices);
        void SetTypeUseGeneratedLods(std::size_t index, bool useGeneratedLods);
        void SetTypeAlignToTerrainNormal(std::size_t index, bool align);
        void SetTypeSourceMeshPath(std::size_t index, const std::string &sourceMeshPath);
        void SetTypeMaterialAssetReference(std::size_t index, const std::string &materialAssetReference);
        void ClearTypeMaterialAssetReference(std::size_t index);
        // Links the type to a .plutofoliage file and takes its settings; empty makes the type local, keeping the
        // values it has. Returns false, changing nothing, when the file cannot be read.
        bool SetTypeAssetReference(std::size_t index, const std::string &assetReference, std::string *errorMessage = nullptr);
        // Replaces a type's settings, rebuilding only what the differences need. The asset reference is kept.
        void ApplyTypeAsset(std::size_t index, FoliageTypeAsset asset);
        // After a .plutofoliage file changed: rereads it into the types linked to it. Returns how many there are.
        std::size_t ReloadTypeAssets(const std::string &assetReference);
        // Shows unsaved settings on the types linked to a file, which stays as it is; ReloadTypeAssets goes back to it.
        // Returns how many there are.
        std::size_t PreviewTypeAsset(const std::string &assetReference, const FoliageTypeAsset &asset);
        void SetTypeScaleRange(std::size_t index, float minScale, float maxScale);
        void SetTypeCollisionEnabled(std::size_t index, bool enabled);
        void SetTypeCollisionCapsule(std::size_t index, const glm::vec3 &center, float radius, float height);
        void SetTypeCellSize(std::size_t index, float cellSize);
        void SetTypeMaxDrawDistance(std::size_t index, float distance);
        // Moves the type's LOD and impostor switches nearer or farther (sanitized; render/LodDistanceScale.h).
        void SetTypeLodDistanceScale(std::size_t index, float scale);
        // How the type bends in the scene's wind (sanitized); changes rebuild the type's render commands.
        void SetTypeWind(std::size_t index, const render::FoliageWindResponse &wind);
        void SetTypeMeshAndMaterials(std::size_t index,
                                     render::Mesh *mesh,
                                     const std::vector<render::Material *> &materials,
                                     const std::string &sourceMeshPath);
        // Foliage draws private copies of its materials (it turns blended leaves into masked ones). This updates a
        // type's copies from `materials`, in place when their layout is unchanged, so the renderer sees a new revision.
        void SyncTypeMaterials(std::size_t index, const std::vector<render::Material *> &materials);
        // After `material` changed or was bound to a model (a saved or extracted asset): updates the types that use it.
        // Returns how many did.
        std::size_t RefreshMaterialAsset(const render::Material *material);

    private:
        enum class HeavyDetail { Omit, Shared };
        enum class MissingInstances { Clear, Keep };
        std::vector<Property> SerializeProperties(HeavyDetail detail) const;
        void DeserializeProperties(const std::vector<Property> &properties, MissingInstances missingInstances);
        FoliageType &EnsureSelectedType();
        void EnsureTypeStorage();
        render::Material *GetMaterialForTypeSubmesh(const FoliageType &type, std::size_t submeshIndex) const;
        void MarkRenderCommandsDirty();
        void MarkInstancesDirty();
        void EnsureStableInstanceIds();
        FoliageCellCoordinate GetCellCoordinate(const FoliageType &type, const glm::vec3 &position) const;
        void RebuildRenderCommandCache(const glm::mat4 &ownerTransform);
        void RebuildTypeMaterialFromReference(FoliageType &type);
        void RebuildTypeMeshFromReference(FoliageType &type);

        bool PaintAtWorldPosition(const FoliageBrush &brush, const glm::vec3 &worldPosition,
                                  const FoliageSurfaceSampler &sampleSurface);
        bool RemoveInstancesAtWorldPosition(const glm::vec3 &worldPosition, float radius);

        render::Material *m_material = nullptr;
        std::vector<FoliageType> m_types;
        int m_selectedTypeIndex = 0;
        mutable std::vector<FoliageInstance> m_emptyInstances;
        std::vector<render::RenderCommand> m_cachedRenderCommands;
        glm::mat4 m_cachedRenderCommandModel = glm::mat4(1.0f);
        bool m_renderCommandCacheDirty = true;
        bool m_hasCachedRenderCommandModel = false;
        std::uint64_t m_nextInstanceId = 1;
        std::uint64_t m_revision = 1;
        // Changes only when instance values do. m_revision also moves for rebuilds that keep them (physics, restores).
        std::uint64_t m_instanceTextRevision = 1;
        mutable std::uint64_t m_collisionCacheRevision = 0;
        mutable glm::mat4 m_collisionCacheOwnerTransform{0.0f};
        mutable std::vector<FoliageCollisionCell> m_cachedCollisionCells;

        float m_maxDrawDistance = 250.0f;
        float m_maxShadowDistance = 80.0f;
        int m_minRenderLod = 0;
        int m_minShadowLod = 0;
        bool m_castShadows = false;
        bool m_isStatic = true;
    };
}
