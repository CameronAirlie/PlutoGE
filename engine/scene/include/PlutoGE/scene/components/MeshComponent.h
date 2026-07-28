#pragma once
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/scene/components/Component.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <string>
#include <vector>

namespace PlutoGE::render
{
    class Mesh;
    class Material;
}

namespace PlutoGE::scene
{
    struct MeshComponentConfig
    {
        render::Mesh *mesh = nullptr;
        render::Material *material = nullptr;
        std::vector<render::Material *> materials;
    };

    class MeshComponent : public TypedComponent<MeshComponent>
    {
    public:
        MeshComponent(const MeshComponentConfig &config)
            : m_mesh(config.mesh), m_material(config.material), m_materials(config.materials)
        {
            if (m_material && m_materials.empty())
            {
                m_materials.push_back(m_material);
            }

            if (!m_material && !m_materials.empty())
            {
                m_material = m_materials.front();
            }

            RefreshMeshDerivedState();
        }
        ~MeshComponent() override = default;

        void Update(float deltaTime) override;
        void SubmitRenderCommands();

        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        void SetMesh(render::Mesh *mesh);
        render::Mesh *GetMesh() const { return m_mesh; }
        void NotifyMeshDataChanged() { MarkRenderCommandsDirty(); }
        bool GenerateLightmapUvAtlasForSubmeshes(const std::vector<size_t> &submeshIndices);
        void SetStatic(bool isStatic)
        {
            if (m_isStatic == isStatic)
            {
                return;
            }

            m_isStatic = isStatic;
            MarkRenderCommandsDirty();
        }
        bool IsStatic() const { return m_isStatic; }
        void SetVisible(bool visible)
        {
            if (m_visible == visible)
            {
                return;
            }

            m_visible = visible;
            MarkRenderCommandsDirty();
        }
        bool IsVisible() const { return m_visible; }
        void SetSubmeshIndex(int submeshIndex)
        {
            if (m_submeshIndex == submeshIndex)
            {
                return;
            }

            m_submeshIndex = submeshIndex;
            MarkRenderCommandsDirty();
        }
        int GetSubmeshIndex() const { return m_submeshIndex; }
        void SetSubmeshRange(int submeshIndex, int submeshCount)
        {
            const int normalizedCount = submeshIndex >= 0 ? std::max(1, submeshCount) : 1;
            if (m_submeshIndex == submeshIndex && m_submeshCount == normalizedCount)
            {
                return;
            }

            m_submeshIndex = submeshIndex;
            m_submeshCount = normalizedCount;
            MarkRenderCommandsDirty();
        }
        int GetSubmeshRangeCount() const { return m_submeshCount; }
        void SetMeshAssetReference(const std::string &meshAssetReference) { m_sourceMeshPath = meshAssetReference; }
        const std::string &GetMeshAssetReference() const { return m_sourceMeshPath; }
        void SetModelObjectIdentity(const std::string &modelAssetId, std::uint64_t localObjectId)
        {
            m_modelAssetId = modelAssetId;
            m_modelObjectId = localObjectId;
        }
        const std::string &GetModelAssetId() const { return m_modelAssetId; }
        std::uint64_t GetModelObjectId() const { return m_modelObjectId; }
        void SetSourceMeshPath(const std::string &sourceMeshPath) { SetMeshAssetReference(sourceMeshPath); }
        const std::string &GetSourceMeshPath() const { return GetMeshAssetReference(); }
        void SetUseGeneratedLods(bool useGeneratedLods) { m_useGeneratedLods = useGeneratedLods; }
        bool GetUseGeneratedLods() const { return m_useGeneratedLods; }
        void SetMeshPositionOffset(const glm::vec3 &offset);
        const glm::vec3 &GetMeshPositionOffset() const;
        void SetMeshRotationOffset(const glm::vec3 &offset);
        const glm::vec3 &GetMeshRotationOffset() const;
        glm::mat4 GetMeshOffsetTransform() const;
        void SetSubmeshPositionOffset(size_t submeshIndex, const glm::vec3 &offset);
        glm::vec3 GetSubmeshPositionOffset(size_t submeshIndex) const;
        void SetSubmeshRotationOffset(size_t submeshIndex, const glm::vec3 &offset);
        glm::vec3 GetSubmeshRotationOffset(size_t submeshIndex) const;
        glm::mat4 GetSubmeshOffsetTransform(size_t submeshIndex) const;

        void SetMaterial(render::Material *material)
        {
            m_material = material;
            if (m_materials.empty())
            {
                m_materials.push_back(material);
            }
            else
            {
                m_materials[0] = material;
            }
            MarkRenderCommandsDirty();
        }
        render::Material *GetMaterial() const { return m_material; }
        void SetMaterials(const std::vector<render::Material *> &materials)
        {
            m_materials = materials;
            m_material = m_materials.empty() ? nullptr : m_materials.front();
            m_submeshMaterials.clear();
            m_materialAssetReferences.clear();
            m_submeshMaterialAssetReferences.clear();
            MarkRenderCommandsDirty();
        }
        const std::vector<render::Material *> &GetMaterials() const { return m_materials; }
        render::Material *GetMaterialForMaterialSlot(size_t materialSlotIndex) const
        {
            if (materialSlotIndex < m_materials.size() && m_materials[materialSlotIndex])
            {
                return m_materials[materialSlotIndex];
            }

            return m_material;
        }
        void SetMaterialForMaterialSlot(size_t materialSlotIndex, render::Material *material)
        {
            if (materialSlotIndex >= m_materials.size())
            {
                m_materials.resize(materialSlotIndex + 1, nullptr);
            }

            m_materials[materialSlotIndex] = material;
            if (materialSlotIndex == 0 || !m_material)
            {
                m_material = material;
            }
            MarkRenderCommandsDirty();
        }
        void SetMaterialAssetForMaterialSlot(size_t materialSlotIndex, const std::string &materialAssetReference)
        {
            if (materialSlotIndex >= m_materialAssetReferences.size())
            {
                m_materialAssetReferences.resize(materialSlotIndex + 1);
            }

            m_materialAssetReferences[materialSlotIndex] = materialAssetReference;
        }
        const std::string &GetMaterialAssetForMaterialSlot(size_t materialSlotIndex) const
        {
            static const std::string empty;
            return materialSlotIndex < m_materialAssetReferences.size() ? m_materialAssetReferences[materialSlotIndex] : empty;
        }
        render::Material *GetMaterialForSubmesh(size_t submeshIndex) const
        {
            if (submeshIndex < m_submeshMaterials.size() && m_submeshMaterials[submeshIndex])
            {
                return m_submeshMaterials[submeshIndex];
            }

            if (m_mesh && submeshIndex < m_mesh->GetSubmeshCount())
            {
                return GetMaterialForMaterialSlot(m_mesh->GetSubmesh(submeshIndex).materialIndex);
            }

            return m_material;
        }
        void SetMaterialForSubmesh(size_t submeshIndex, render::Material *material)
        {
            if (submeshIndex >= m_submeshMaterials.size())
            {
                m_submeshMaterials.resize(submeshIndex + 1, nullptr);
            }

            m_submeshMaterials[submeshIndex] = material;
            MarkRenderCommandsDirty();
        }
        void SetMaterialAssetForSubmesh(size_t submeshIndex, const std::string &materialAssetReference)
        {
            if (submeshIndex >= m_submeshMaterialAssetReferences.size())
            {
                m_submeshMaterialAssetReferences.resize(submeshIndex + 1);
            }

            m_submeshMaterialAssetReferences[submeshIndex] = materialAssetReference;
        }
        const std::string &GetMaterialAssetForSubmesh(size_t submeshIndex) const
        {
            static const std::string empty;
            return submeshIndex < m_submeshMaterialAssetReferences.size() ? m_submeshMaterialAssetReferences[submeshIndex] : empty;
        }
        bool HasMaterialOverrideForSubmesh(size_t submeshIndex) const
        {
            return submeshIndex < m_submeshMaterials.size() && m_submeshMaterials[submeshIndex] != nullptr;
        }
        render::Material *CreateUniqueMaterialForMaterialSlot(size_t materialSlotIndex);
        render::Material *CreateUniqueMaterialForSubmesh(size_t submeshIndex);
        bool CreateSkeletonAttachmentEntities();

    private:
        void MarkRenderCommandsDirty();
        void RefreshMeshDerivedState();
        void UpdateCachedPreviousModels(const glm::mat4 &modelMatrix);
        MeshComponent *FindMeshOffsetSource() const;

        render::Mesh *m_mesh = nullptr;
        render::Material *m_material = nullptr;
        std::vector<render::Material *> m_materials;
        std::vector<render::Material *> m_submeshMaterials;
        std::vector<std::string> m_materialAssetReferences;
        std::vector<std::string> m_submeshMaterialAssetReferences;
        glm::mat4 m_previousModelMatrix = glm::mat4(1.0f);
        bool m_hasPreviousModelMatrix = false;
        bool m_isStatic = false;
        bool m_renderCommandCacheDirty = true;
        bool m_hasCachedRenderCommandModel = false;
        glm::mat4 m_cachedRenderCommandModel = glm::mat4(1.0f);
        std::vector<render::RenderCommand> m_cachedRenderCommands;
        std::string m_sourceMeshPath;
        std::string m_modelAssetId;
        std::uint64_t m_modelObjectId = 0;
        glm::vec3 m_meshPositionOffset{0.0f};
        glm::vec3 m_meshRotationOffset{0.0f};
        std::vector<glm::vec3> m_submeshPositionOffsets;
        std::vector<glm::vec3> m_submeshRotationOffsets;
        int m_submeshIndex = -1;
        int m_submeshCount = 1;
        bool m_visible = true;
        bool m_useGeneratedLods = false;
        bool m_hasAnimatedNodeSubmeshes = false;
        std::vector<size_t> m_generatedLightmapUvSubmeshes;
    };
}
