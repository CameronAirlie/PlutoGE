#include "PlutoGE/scene/components/ClothComponent.h"

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/MeshComponent.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace PlutoGE::scene
{
    namespace
    {
        constexpr float kMinExtent = 0.05f;
        constexpr int kMinSegments = 1;
        constexpr int kMaxSegments = 64;
        constexpr int kMaxSolverIterations = 16;
        constexpr float kMaxFrameTime = 1.0f / 15.0f;
        constexpr float kMaxSimulationStep = 1.0f / 120.0f;

        glm::vec3 SafeNormalize(const glm::vec3 &value, const glm::vec3 &fallback)
        {
            const float lengthSquared = glm::dot(value, value);
            if (lengthSquared <= 1e-8f)
            {
                return fallback;
            }

            return value * glm::inversesqrt(lengthSquared);
        }
    }

    ClothComponent::~ClothComponent()
    {
        if (auto *meshComponent = ResolveMeshComponent())
        {
            if (meshComponent->GetMesh() == m_runtimeMesh)
            {
                meshComponent->SetMesh(nullptr);
            }
        }

        delete m_runtimeMesh;
        m_runtimeMesh = nullptr;
    }

    glm::vec3 ClothComponent::GetSimulatedVertexPosition(std::size_t index) const
    {
        return index < m_particles.size() ? m_particles[index].position : glm::vec3(0.0f);
    }

    void ClothComponent::ResetSimulation()
    {
        m_simulationTime = 0.0f;
        RebuildSimulation();
    }

    void ClothComponent::Update(float deltaTime)
    {
        EnsureMeshBinding();

        if (m_simulationDirty)
        {
            RebuildSimulation();
        }

        if (m_particles.empty())
        {
            return;
        }

        const float clampedDeltaTime = std::clamp(deltaTime, 0.0f, kMaxFrameTime);
        if (clampedDeltaTime > 0.0f)
        {
            const int substepCount = std::max(1, static_cast<int>(std::ceil(clampedDeltaTime / kMaxSimulationStep)));
            const float substepTime = clampedDeltaTime / static_cast<float>(substepCount);
            for (int substep = 0; substep < substepCount; ++substep)
            {
                StepSimulation(substepTime);
                m_simulationTime += substepTime;
            }
            if (m_simulationTime > 100000.0f)
            {
                m_simulationTime -= 100000.0f;
            }
            m_meshDirty = true;
        }

        if (m_meshDirty)
        {
            UploadRuntimeMesh();
        }
    }

    std::vector<Property> ClothComponent::Serialize() const
    {
        return {
            {"Width", PropertyType::Float, std::to_string(m_width)},
            {"Height", PropertyType::Float, std::to_string(m_height)},
            {"SegmentsX", PropertyType::Int, std::to_string(m_segmentsX)},
            {"SegmentsY", PropertyType::Int, std::to_string(m_segmentsY)},
            {"Gravity", PropertyType::Float, std::to_string(m_gravity)},
            {"Damping", PropertyType::Float, std::to_string(m_damping)},
            {"ConstraintStiffness", PropertyType::Float, std::to_string(m_constraintStiffness)},
            {"SolverIterations", PropertyType::Int, std::to_string(m_solverIterations)},
            {"WindAmplitude", PropertyType::Float, std::to_string(m_windAmplitude)},
            {"WindFrequency", PropertyType::Float, std::to_string(m_windFrequency)},
            {"PinTopEdge", PropertyType::Bool, m_pinTopEdge ? "true" : "false"},
            {"CollideWithFloor", PropertyType::Bool, m_collideWithFloor ? "true" : "false"},
            {"FloorOffset", PropertyType::Float, std::to_string(m_floorOffset)},
        };
    }

    void ClothComponent::Deserialize(const std::vector<Property> &properties)
    {
        bool topologyChanged = false;
        for (const auto &property : properties)
        {
            if (property.name == "Width")
            {
                m_width = std::max(std::stof(property.value), kMinExtent);
                topologyChanged = true;
            }
            else if (property.name == "Height")
            {
                m_height = std::max(std::stof(property.value), kMinExtent);
                topologyChanged = true;
            }
            else if (property.name == "SegmentsX")
            {
                m_segmentsX = std::clamp(std::stoi(property.value), kMinSegments, kMaxSegments);
                topologyChanged = true;
            }
            else if (property.name == "SegmentsY")
            {
                m_segmentsY = std::clamp(std::stoi(property.value), kMinSegments, kMaxSegments);
                topologyChanged = true;
            }
            else if (property.name == "Gravity")
            {
                m_gravity = std::max(std::stof(property.value), 0.0f);
            }
            else if (property.name == "Damping")
            {
                m_damping = std::clamp(std::stof(property.value), 0.8f, 1.0f);
            }
            else if (property.name == "ConstraintStiffness")
            {
                m_constraintStiffness = std::clamp(std::stof(property.value), 0.0f, 1.0f);
            }
            else if (property.name == "SolverIterations")
            {
                m_solverIterations = std::clamp(std::stoi(property.value), 1, kMaxSolverIterations);
            }
            else if (property.name == "WindAmplitude")
            {
                m_windAmplitude = std::max(std::stof(property.value), 0.0f);
            }
            else if (property.name == "WindFrequency")
            {
                m_windFrequency = std::max(std::stof(property.value), 0.0f);
            }
            else if (property.name == "PinTopEdge")
            {
                m_pinTopEdge = property.value == "true" || property.value == "1";
                topologyChanged = true;
            }
            else if (property.name == "CollideWithFloor")
            {
                m_collideWithFloor = property.value == "true" || property.value == "1";
            }
            else if (property.name == "FloorOffset")
            {
                m_floorOffset = std::stof(property.value);
            }
        }

        if (topologyChanged)
        {
            m_simulationDirty = true;
        }
    }

    void ClothComponent::RebuildSimulation()
    {
        m_particles.clear();
        m_constraints.clear();

        const int columns = m_segmentsX + 1;
        const int rows = m_segmentsY + 1;
        m_particles.resize(static_cast<std::size_t>(columns * rows));
        for (int y = 0; y < rows; ++y)
        {
            for (int x = 0; x < columns; ++x)
            {
                auto &particle = m_particles[VertexIndex(x, y)];
                particle.restPosition = BuildRestPosition(x, y);
                particle.position = particle.restPosition;
                particle.previousPosition = particle.restPosition;
                particle.pinned = m_pinTopEdge && y == 0;
            }
        }

        BuildConstraints();
        BuildRuntimeMesh();
        m_simulationDirty = false;
        m_meshDirty = true;
    }

    void ClothComponent::EnsureMeshBinding()
    {
        auto *owner = GetOwner();
        if (!owner)
        {
            return;
        }

        auto *meshComponent = owner->GetComponent<MeshComponent>();
        if (!meshComponent)
        {
            auto &engine = core::Engine::GetInstance();
            auto *defaultMaterial = engine.GetAssetManager().LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference));
            meshComponent = owner->CreateComponent<MeshComponent>(MeshComponentConfig{
                .mesh = m_runtimeMesh,
                .material = defaultMaterial,
                .materials = {defaultMaterial},
            });
            if (meshComponent)
            {
                meshComponent->SetMaterialAssetForMaterialSlot(0, std::string(assets::Project::kBuiltinDefaultShadedMaterialReference));
                meshComponent->SetStatic(false);
            }
        }

        if (meshComponent)
        {
            if (meshComponent->GetMesh() != m_runtimeMesh)
            {
                meshComponent->SetMesh(m_runtimeMesh);
            }
            meshComponent->SetStatic(false);
        }
    }

    MeshComponent *ClothComponent::ResolveMeshComponent() const
    {
        auto *owner = GetOwner();
        return owner ? owner->GetComponent<MeshComponent>() : nullptr;
    }

    void ClothComponent::BuildConstraints()
    {
        auto addConstraint = [this](int ax, int ay, int bx, int by)
        {
            const std::size_t indexA = VertexIndex(ax, ay);
            const std::size_t indexB = VertexIndex(bx, by);
            const float restLength = glm::length(m_particles[indexB].restPosition - m_particles[indexA].restPosition);
            m_constraints.push_back(Constraint{
                .a = static_cast<std::uint32_t>(indexA),
                .b = static_cast<std::uint32_t>(indexB),
                .restLength = restLength,
            });
        };

        for (int y = 0; y <= m_segmentsY; ++y)
        {
            for (int x = 0; x <= m_segmentsX; ++x)
            {
                if (x < m_segmentsX)
                {
                    addConstraint(x, y, x + 1, y);
                }
                if (y < m_segmentsY)
                {
                    addConstraint(x, y, x, y + 1);
                }
                if (x < m_segmentsX && y < m_segmentsY)
                {
                    addConstraint(x, y, x + 1, y + 1);
                    addConstraint(x + 1, y, x, y + 1);
                }
                if (x + 2 <= m_segmentsX)
                {
                    addConstraint(x, y, x + 2, y);
                }
                if (y + 2 <= m_segmentsY)
                {
                    addConstraint(x, y, x, y + 2);
                }
            }
        }
    }

    void ClothComponent::BuildRuntimeMesh()
    {
        render::MeshData meshData;
        // Keep separate vertices for the back face so it can have correctly
        // inverted normals while the renderer continues to use back-face culling.
        const std::size_t particleCount = m_particles.size();
        meshData.vertices.resize(particleCount * 2);
        meshData.indices.reserve(static_cast<std::size_t>(m_segmentsX * m_segmentsY * 12));

        const int columns = m_segmentsX + 1;
        for (int y = 0; y <= m_segmentsY; ++y)
        {
            for (int x = 0; x <= m_segmentsX; ++x)
            {
                const std::size_t vertexIndex = VertexIndex(x, y);
                auto &vertex = meshData.vertices[vertexIndex];
                const glm::vec3 position = m_particles[vertexIndex].position;
                const float u = static_cast<float>(x) / static_cast<float>(m_segmentsX);
                const float v = static_cast<float>(y) / static_cast<float>(m_segmentsY);
                vertex.position = {position.x, position.y, position.z};
                vertex.normal = {0.0f, 0.0f, 1.0f};
                vertex.uv = {u, v};
                vertex.tangent = {1.0f, 0.0f, 0.0f, 1.0f};
                vertex.uv2 = {u, v};
                auto &backVertex = meshData.vertices[particleCount + vertexIndex];
                backVertex = vertex;
                backVertex.normal = {0.0f, 0.0f, -1.0f};
                backVertex.tangent = {1.0f, 0.0f, 0.0f, -1.0f};
            }
        }

        for (int y = 0; y < m_segmentsY; ++y)
        {
            for (int x = 0; x < m_segmentsX; ++x)
            {
                const unsigned int topLeft = static_cast<unsigned int>(y * columns + x);
                const unsigned int topRight = topLeft + 1;
                const unsigned int bottomLeft = static_cast<unsigned int>((y + 1) * columns + x);
                const unsigned int bottomRight = bottomLeft + 1;

                meshData.indices.push_back(topLeft);
                meshData.indices.push_back(bottomLeft);
                meshData.indices.push_back(topRight);
                meshData.indices.push_back(topRight);
                meshData.indices.push_back(bottomLeft);
                meshData.indices.push_back(bottomRight);

                const unsigned int backOffset = static_cast<unsigned int>(particleCount);
                meshData.indices.push_back(backOffset + topRight);
                meshData.indices.push_back(backOffset + bottomLeft);
                meshData.indices.push_back(backOffset + topLeft);
                meshData.indices.push_back(backOffset + bottomRight);
                meshData.indices.push_back(backOffset + bottomLeft);
                meshData.indices.push_back(backOffset + topRight);
            }
        }

        render::MeshConfig config;
        config.data = std::move(meshData);
        config.submeshes.push_back(render::Submesh{
            .indexOffset = 0,
            .indexCount = static_cast<std::uint32_t>(config.data.indices.size()),
            .materialIndex = 0,
        });

        delete m_runtimeMesh;
        m_runtimeMesh = render::Mesh::CreateInitialized(config);
        if (auto *meshComponent = ResolveMeshComponent())
        {
            meshComponent->SetMesh(m_runtimeMesh);
            meshComponent->NotifyMeshDataChanged();
        }
    }

    void ClothComponent::StepSimulation(float deltaTime)
    {
        glm::vec3 gravity(0.0f, -m_gravity, 0.0f);
        // Simulation vertices are mesh-local. Convert world gravity to local
        // space so rotating or parenting the cloth does not rotate gravity.
        if (const auto *owner = GetOwner())
        {
            const glm::mat3 worldBasis(owner->GetWorldTransform());
            if (std::abs(glm::determinant(worldBasis)) > 1e-8f)
            {
                gravity = glm::inverse(worldBasis) * gravity;
            }
        }
        const float phase = m_simulationTime * m_windFrequency;
        const glm::vec3 wind(
            std::sin(phase) * m_windAmplitude,
            std::cos(phase * 0.37f) * m_windAmplitude * 0.15f,
            std::cos(phase * 1.23f) * m_windAmplitude);
        const glm::vec3 acceleration = gravity + wind;
        const float timeStepSquared = deltaTime * deltaTime;

        for (auto &particle : m_particles)
        {
            if (particle.pinned)
            {
                particle.position = particle.restPosition;
                particle.previousPosition = particle.restPosition;
                continue;
            }

            const glm::vec3 velocity = (particle.position - particle.previousPosition) * m_damping;
            const glm::vec3 nextPosition = particle.position + velocity + acceleration * timeStepSquared;
            particle.previousPosition = particle.position;
            particle.position = nextPosition;
        }

        for (int iteration = 0; iteration < m_solverIterations; ++iteration)
        {
            SatisfyConstraints();
            ApplyFloorConstraint();
        }
    }

    void ClothComponent::SatisfyConstraints()
    {
        for (const auto &constraint : m_constraints)
        {
            auto &particleA = m_particles[constraint.a];
            auto &particleB = m_particles[constraint.b];

            const glm::vec3 delta = particleB.position - particleA.position;
            const float lengthSquared = glm::dot(delta, delta);
            if (lengthSquared <= 1e-8f)
            {
                continue;
            }

            const float distance = std::sqrt(lengthSquared);
            const float error = (distance - constraint.restLength) / distance;
            const float inverseMassA = particleA.pinned ? 0.0f : 1.0f;
            const float inverseMassB = particleB.pinned ? 0.0f : 1.0f;
            const float inverseMassSum = inverseMassA + inverseMassB;
            if (inverseMassSum <= 0.0f)
            {
                continue;
            }
            const glm::vec3 correction = delta * (m_constraintStiffness * error / inverseMassSum);

            if (!particleA.pinned)
            {
                particleA.position += correction * inverseMassA;
            }
            if (!particleB.pinned)
            {
                particleB.position -= correction * inverseMassB;
            }
        }

        for (auto &particle : m_particles)
        {
            if (particle.pinned)
            {
                particle.position = particle.restPosition;
                particle.previousPosition = particle.restPosition;
            }
        }
    }

    void ClothComponent::ApplyFloorConstraint()
    {
        if (!m_collideWithFloor)
        {
            return;
        }

        const auto *owner = GetOwner();
        const glm::mat4 worldTransform = owner ? owner->GetWorldTransform() : glm::mat4(1.0f);
        const float determinant = glm::determinant(glm::mat3(worldTransform));
        const glm::mat4 inverseWorld = std::abs(determinant) > 1e-8f ? glm::inverse(worldTransform) : glm::mat4(1.0f);
        for (auto &particle : m_particles)
        {
            glm::vec3 worldPosition(worldTransform * glm::vec4(particle.position, 1.0f));
            if (!particle.pinned && worldPosition.y < m_floorOffset)
            {
                worldPosition.y = m_floorOffset;
                particle.position = glm::vec3(inverseWorld * glm::vec4(worldPosition, 1.0f));
            }
        }
    }

    void ClothComponent::UploadRuntimeMesh()
    {
        if (!m_runtimeMesh || m_particles.empty())
        {
            return;
        }

        auto meshData = m_runtimeMesh->GetMeshData();
        const std::size_t particleCount = m_particles.size();
        if (meshData.vertices.size() != particleCount * 2)
        {
            return;
        }

        for (std::size_t index = 0; index < m_particles.size(); ++index)
        {
            const glm::vec3 position = m_particles[index].position;
            meshData.vertices[index].position = {position.x, position.y, position.z};
            meshData.vertices[index].normal = {0.0f, 0.0f, 0.0f};
            meshData.vertices[index].tangent = {1.0f, 0.0f, 0.0f, 1.0f};
            meshData.vertices[particleCount + index].position = {position.x, position.y, position.z};
            meshData.vertices[particleCount + index].normal = {0.0f, 0.0f, 0.0f};
            meshData.vertices[particleCount + index].tangent = {1.0f, 0.0f, 0.0f, -1.0f};
        }

        constexpr std::size_t kIndicesPerClothCell = 12;
        constexpr std::size_t kFrontIndicesPerClothCell = 6;
        for (std::size_t cellIndexOffset = 0;
             cellIndexOffset + kIndicesPerClothCell <= meshData.indices.size();
             cellIndexOffset += kIndicesPerClothCell)
        {
            for (std::size_t triangleOffset = 0;
                 triangleOffset < kFrontIndicesPerClothCell;
                 triangleOffset += 3)
            {
                const std::size_t triangle = cellIndexOffset + triangleOffset;
                const auto index0 = meshData.indices[triangle];
                const auto index1 = meshData.indices[triangle + 1];
                const auto index2 = meshData.indices[triangle + 2];
                if (index0 >= particleCount || index1 >= particleCount || index2 >= particleCount)
                {
                    continue;
                }

                const glm::vec3 p0 = m_particles[index0].position;
                const glm::vec3 p1 = m_particles[index1].position;
                const glm::vec3 p2 = m_particles[index2].position;
                const glm::vec3 faceNormal = glm::cross(p1 - p0, p2 - p0);
                for (const auto index : {index0, index1, index2})
                {
                    glm::vec3 normal(meshData.vertices[index].normal[0],
                                     meshData.vertices[index].normal[1],
                                     meshData.vertices[index].normal[2]);
                    normal += faceNormal;
                    meshData.vertices[index].normal = {normal.x, normal.y, normal.z};
                }
            }
        }

        for (std::size_t index = 0; index < particleCount; ++index)
        {
            auto &vertex = meshData.vertices[index];
            const glm::vec3 normal = SafeNormalize(glm::vec3(vertex.normal[0], vertex.normal[1], vertex.normal[2]), glm::vec3(0.0f, 0.0f, 1.0f));
            const glm::vec3 tangent = SafeNormalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), normal), glm::vec3(1.0f, 0.0f, 0.0f));
            vertex.normal = {normal.x, normal.y, normal.z};
            vertex.tangent = {tangent.x, tangent.y, tangent.z, 1.0f};
            auto &backVertex = meshData.vertices[particleCount + index];
            backVertex.normal = {-normal.x, -normal.y, -normal.z};
            backVertex.tangent = {tangent.x, tangent.y, tangent.z, -1.0f};
        }

        m_runtimeMesh->UpdateVertexData(meshData.vertices);
        if (auto *meshComponent = ResolveMeshComponent())
        {
            meshComponent->NotifyMeshDataChanged();
        }
        m_meshDirty = false;
    }

    std::size_t ClothComponent::VertexIndex(int x, int y) const
    {
        return static_cast<std::size_t>(y * (m_segmentsX + 1) + x);
    }

    glm::vec3 ClothComponent::BuildRestPosition(int x, int y) const
    {
        const float normalizedX = static_cast<float>(x) / static_cast<float>(m_segmentsX);
        const float normalizedY = static_cast<float>(y) / static_cast<float>(m_segmentsY);
        return glm::vec3(
            (normalizedX - 0.5f) * m_width,
            -normalizedY * m_height,
            0.0f);
    }
}
