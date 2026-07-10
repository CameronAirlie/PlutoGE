#pragma once

#include "PlutoGE/scene/components/Component.h"

#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace PlutoGE::render
{
    class Mesh;
}

namespace PlutoGE::scene
{
    class MeshComponent;

    class ClothComponent : public TypedComponent<ClothComponent>
    {
    public:
        ClothComponent() = default;
        ~ClothComponent() override;

        void Update(float deltaTime) override;
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        float GetWidth() const { return m_width; }
        float GetHeight() const { return m_height; }
        int GetSegmentsX() const { return m_segmentsX; }
        int GetSegmentsY() const { return m_segmentsY; }
        float GetGravity() const { return m_gravity; }
        float GetDamping() const { return m_damping; }
        float GetConstraintStiffness() const { return m_constraintStiffness; }
        int GetSolverIterations() const { return m_solverIterations; }
        float GetWindAmplitude() const { return m_windAmplitude; }
        float GetWindFrequency() const { return m_windFrequency; }
        bool GetPinTopEdge() const { return m_pinTopEdge; }
        bool GetCollideWithFloor() const { return m_collideWithFloor; }
        float GetFloorOffset() const { return m_floorOffset; }
        std::size_t GetSimulatedVertexCount() const { return m_particles.size(); }
        glm::vec3 GetSimulatedVertexPosition(std::size_t index) const;

        void ResetSimulation();

    private:
        struct Particle
        {
            glm::vec3 position{0.0f};
            glm::vec3 previousPosition{0.0f};
            glm::vec3 restPosition{0.0f};
            bool pinned = false;
        };

        struct Constraint
        {
            std::uint32_t a = 0;
            std::uint32_t b = 0;
            float restLength = 0.0f;
        };

        float m_width = 1.2f;
        float m_height = 1.6f;
        int m_segmentsX = 14;
        int m_segmentsY = 18;
        float m_gravity = 9.81f;
        float m_damping = 0.992f;
        float m_constraintStiffness = 0.92f;
        int m_solverIterations = 5;
        float m_windAmplitude = 0.35f;
        float m_windFrequency = 1.35f;
        bool m_pinTopEdge = true;
        bool m_collideWithFloor = true;
        float m_floorOffset = 0.0f;

        render::Mesh *m_runtimeMesh = nullptr;
        std::vector<Particle> m_particles;
        std::vector<Constraint> m_constraints;
        float m_simulationTime = 0.0f;
        bool m_simulationDirty = true;
        bool m_meshDirty = true;

        void RebuildSimulation();
        void EnsureMeshBinding();
        MeshComponent *ResolveMeshComponent() const;
        void BuildConstraints();
        void BuildRuntimeMesh();
        void StepSimulation(float deltaTime);
        void SatisfyConstraints();
        void ApplyFloorConstraint();
        void UploadRuntimeMesh();
        std::size_t VertexIndex(int x, int y) const;
        glm::vec3 BuildRestPosition(int x, int y) const;
    };
}