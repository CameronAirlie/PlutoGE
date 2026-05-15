#pragma once
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/components/Component.h"

#include <glm/glm.hpp>
#include <string>

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
        }
        ~MeshComponent() override = default;

        void Update(float deltaTime) override;

        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        void SetMesh(render::Mesh *mesh);
        render::Mesh *GetMesh() const { return m_mesh; }
        void SetStatic(bool isStatic) { m_isStatic = isStatic; }
        bool IsStatic() const { return m_isStatic; }
        void SetSourceMeshPath(const std::string &sourceMeshPath) { m_sourceMeshPath = sourceMeshPath; }
        const std::string &GetSourceMeshPath() const { return m_sourceMeshPath; }

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
        }
        render::Material *GetMaterial() const { return m_material; }
        void SetMaterials(const std::vector<render::Material *> &materials)
        {
            m_materials = materials;
            m_material = m_materials.empty() ? nullptr : m_materials.front();
            m_submeshMaterials.clear();
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
        }
        render::Material *CreateUniqueMaterialForMaterialSlot(size_t materialSlotIndex);
        render::Material *CreateUniqueMaterialForSubmesh(size_t submeshIndex);

    private:
        render::Mesh *m_mesh = nullptr;
        render::Material *m_material = nullptr;
        std::vector<render::Material *> m_materials;
        std::vector<render::Material *> m_submeshMaterials;
        glm::mat4 m_previousModelMatrix = glm::mat4(1.0f);
        bool m_hasPreviousModelMatrix = false;
        bool m_isStatic = false;
        std::string m_sourceMeshPath;
    };
}