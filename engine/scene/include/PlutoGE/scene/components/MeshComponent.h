#pragma once
#include "Component.h"

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
    };

    class MeshComponent : public TypedComponent<MeshComponent>
    {
    public:
        MeshComponent(const MeshComponentConfig &config)
            : m_mesh(config.mesh), m_material(config.material) {}
        ~MeshComponent() override = default;

        void Update(float deltaTime) override;

        void SetMesh(render::Mesh *mesh) { m_mesh = mesh; }
        render::Mesh *GetMesh() const { return m_mesh; }

        void SetMaterial(render::Material *material) { m_material = material; }
        render::Material *GetMaterial() const { return m_material; }

    private:
        render::Mesh *m_mesh = nullptr;
        render::Material *m_material = nullptr;
    };
}