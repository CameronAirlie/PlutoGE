#pragma once

#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/scene/FoliageTypeAsset.h"
#include "PlutoGE/scene/components/Component.h"

#include <glm/glm.hpp>
#include <functional>
#include <cstdint>
#include <memory>
#include <string>
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
        std::string sourceMeshPath;
        std::string materialAssetReference;
        render::Mesh *mesh = nullptr;
        render::Material *materialOverride = nullptr;
        std::vector<render::Material *> materials;
        std::vector<std::unique_ptr<render::Material>> ownedMaterials;
        std::vector<FoliageInstance> instances;
        int submeshIndex = -1;
        std::vector<int> submeshIndices;
        bool useGeneratedLods = false;
        FoliageTypeAsset asset;
    };

    enum class FoliageBrushMode
    {
        Add,
        Remove,
    };

    class FoliageComponent : public TypedComponent<FoliageComponent>
    {
    public:
        FoliageComponent() = default;
        ~FoliageComponent() override = default;

        void Update(float deltaTime) override;
        void SubmitRenderCommands();

        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        void SetMesh(render::Mesh *mesh);
        render::Mesh *GetMesh() const;
        void SetMaterial(render::Material *material) { m_material = material; }
        render::Material *GetMaterial() const { return m_material; }
        void SetSourceMeshPath(const std::string &sourceMeshPath);
        const std::string &GetSourceMeshPath() const;
        void SetMaterialAssetReference(const std::string &materialAssetReference);
        const std::string &GetMaterialAssetReference() const;
        void ClearMaterialAssetReference();

        void SetPaintEnabled(bool enabled) { m_paintEnabled = enabled; }
        bool IsPaintEnabled() const { return m_paintEnabled; }
        void SetBrushMode(FoliageBrushMode mode) { m_brushMode = mode; }
        FoliageBrushMode GetBrushMode() const { return m_brushMode; }
        void SetBrushRadius(float radius);
        float GetBrushRadius() const { return m_brushRadius; }
        void SetDensity(int density);
        int GetDensity() const { return m_density; }
        void SetScaleRange(float minScale, float maxScale);
        float GetMinScale() const { return m_minScale; }
        float GetMaxScale() const { return m_maxScale; }
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

        bool ApplyBrushAtWorldPosition(
            const glm::vec3 &worldPosition,
            const glm::vec3 &terrainNormal,
            const std::function<float(float, float)> &sampleTerrainHeight);

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
        std::size_t SnapSelectedTypeInstancesToSurface(const std::function<float(float, float)> &sampleTerrainHeight);
        std::size_t SnapAllInstancesToSurface(const std::function<float(float, float)> &sampleTerrainHeight);
        bool RemoveSelectedTypeInstance(std::size_t instanceIndex);
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
        void SetTypeSourceMeshPath(std::size_t index, const std::string &sourceMeshPath);
        void SetTypeMaterialAssetReference(std::size_t index, const std::string &materialAssetReference);
        void ClearTypeMaterialAssetReference(std::size_t index);
        bool SetTypeAssetReference(std::size_t index, const std::string &assetReference);
        void SetTypeCollisionEnabled(std::size_t index, bool enabled);
        void SetTypeCollisionCapsule(std::size_t index, const glm::vec3 &center, float radius, float height);
        void SetTypeCellSize(std::size_t index, float cellSize);
        void SetTypeMaxDrawDistance(std::size_t index, float distance);
        void SetTypeMeshAndMaterials(std::size_t index,
                                     render::Mesh *mesh,
                                     const std::vector<render::Material *> &materials,
                                     const std::string &sourceMeshPath);

    private:
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

        bool PaintAtWorldPosition(const glm::vec3 &worldPosition,
                                  const glm::vec3 &terrainNormal,
                                  const std::function<float(float, float)> &sampleTerrainHeight);
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
        mutable std::uint64_t m_collisionCacheRevision = 0;
        mutable glm::mat4 m_collisionCacheOwnerTransform{0.0f};
        mutable std::vector<FoliageCollisionCell> m_cachedCollisionCells;

        bool m_paintEnabled = false;
        FoliageBrushMode m_brushMode = FoliageBrushMode::Add;
        float m_brushRadius = 5.0f;
        int m_density = 3;
        float m_minScale = 0.8f;
        float m_maxScale = 1.2f;
        float m_maxDrawDistance = 250.0f;
        float m_maxShadowDistance = 80.0f;
        int m_minRenderLod = 2;
        int m_minShadowLod = 3;
        bool m_castShadows = false;
        bool m_isStatic = true;
    };
}
