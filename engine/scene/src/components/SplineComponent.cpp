#include "PlutoGE/scene/components/SplineComponent.h"

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace PlutoGE::scene
{
    namespace
    {
        std::string SerializeVec3(const glm::vec3 &value)
        {
            return std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z);
        }

        glm::vec3 ParseVec3(const std::string &value, const glm::vec3 &fallback = glm::vec3(0.0f))
        {
            glm::vec3 parsedValue = fallback;
            sscanf_s(value.c_str(), "%f,%f,%f", &parsedValue.x, &parsedValue.y, &parsedValue.z);
            return parsedValue;
        }

        bool ParseBool(const std::string &value)
        {
            return value == "true" || value == "True" || value == "1";
        }

        glm::vec3 CatmullRom(const glm::vec3 &p0, const glm::vec3 &p1, const glm::vec3 &p2, const glm::vec3 &p3, float t)
        {
            const float t2 = t * t;
            const float t3 = t2 * t;
            return 0.5f * ((2.0f * p1) +
                           (-p0 + p2) * t +
                           (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                           (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
        }

        glm::vec3 GetWrappedPoint(const std::vector<SplineControlPoint> &points, int index, bool closed)
        {
            const int count = static_cast<int>(points.size());
            if (count == 0)
            {
                return glm::vec3(0.0f);
            }

            if (closed)
            {
                index %= count;
                if (index < 0)
                {
                    index += count;
                }
                return points[static_cast<std::size_t>(index)].position;
            }

            return points[static_cast<std::size_t>(std::clamp(index, 0, count - 1))].position;
        }

        void AddVertex(render::MeshData &meshData,
                       const glm::vec3 &position,
                       const glm::vec3 &normal,
                       const glm::vec2 &uv,
                       const glm::vec3 &tangent)
        {
            meshData.vertices.push_back(render::MeshVertexData{
                {position.x, position.y, position.z},
                {normal.x, normal.y, normal.z},
                {uv.x, uv.y},
                {tangent.x, tangent.y, tangent.z, 1.0f},
            });
        }

        void AddQuad(render::MeshData &meshData, unsigned int a, unsigned int b, unsigned int c, unsigned int d)
        {
            meshData.indices.push_back(a);
            meshData.indices.push_back(b);
            meshData.indices.push_back(c);
            meshData.indices.push_back(c);
            meshData.indices.push_back(b);
            meshData.indices.push_back(d);
        }

        std::vector<glm::vec3> BuildSplineCenters(const std::vector<SplineControlPoint> &points,
                                                  bool closed,
                                                  int samplesPerSegment)
        {
            std::vector<glm::vec3> centers;
            if (points.size() < 2)
            {
                return centers;
            }

            const int segmentCount = closed ? static_cast<int>(points.size()) : static_cast<int>(points.size()) - 1;
            if (segmentCount <= 0)
            {
                return centers;
            }

            centers.reserve(static_cast<std::size_t>(segmentCount * samplesPerSegment + 1));
            for (int segment = 0; segment < segmentCount; ++segment)
            {
                for (int sample = 0; sample < samplesPerSegment; ++sample)
                {
                    const float t = static_cast<float>(sample) / static_cast<float>(samplesPerSegment);
                    centers.push_back(CatmullRom(
                        GetWrappedPoint(points, segment - 1, closed),
                        GetWrappedPoint(points, segment, closed),
                        GetWrappedPoint(points, segment + 1, closed),
                        GetWrappedPoint(points, segment + 2, closed),
                        t));
                }
            }

            if (!closed)
            {
                centers.push_back(points.back().position);
            }

            return centers;
        }

        std::unique_ptr<render::Mesh> BuildSplineMesh(const std::vector<glm::vec3> &centers,
                                                      float width,
                                                      float thickness,
                                                      float uvMetersPerTile,
                                                      bool closed,
                                                      bool includeSideFaces = true)
        {
            if (centers.size() < 2)
            {
                return nullptr;
            }

            render::MeshData meshData;
            const float halfWidth = width * 0.5f;
            const float bottomOffset = std::max(thickness, 0.0f);
            float distance = 0.0f;
            std::vector<float> distances(centers.size(), 0.0f);
            for (std::size_t index = 1; index < centers.size(); ++index)
            {
                distance += glm::length(centers[index] - centers[index - 1]);
                distances[index] = distance;
            }

            const glm::vec3 up(0.0f, 1.0f, 0.0f);
            for (std::size_t index = 0; index < centers.size(); ++index)
            {
                const glm::vec3 previous = index == 0 ? (closed ? centers[centers.size() - 1] : centers[index]) : centers[index - 1];
                const glm::vec3 next = index + 1 < centers.size() ? centers[index + 1] : (closed ? centers[0] : centers[index]);
                glm::vec3 tangent = next - previous;
                if (glm::dot(tangent, tangent) <= 0.000001f)
                {
                    tangent = glm::vec3(0.0f, 0.0f, 1.0f);
                }
                tangent = glm::normalize(tangent);

                glm::vec3 right = glm::cross(tangent, up);
                if (glm::dot(right, right) <= 0.000001f)
                {
                    right = glm::vec3(1.0f, 0.0f, 0.0f);
                }
                right = glm::normalize(right);
                const glm::vec3 surfaceNormal = glm::normalize(glm::cross(right, tangent));

                const float v = distances[index] / uvMetersPerTile;
                AddVertex(meshData, centers[index] - right * halfWidth, surfaceNormal, glm::vec2(0.0f, v), tangent);
                AddVertex(meshData, centers[index] + right * halfWidth, surfaceNormal, glm::vec2(1.0f, v), tangent);
                AddVertex(meshData, centers[index] - right * halfWidth - up * bottomOffset, -right, glm::vec2(0.0f, v), tangent);
                AddVertex(meshData, centers[index] + right * halfWidth - up * bottomOffset, right, glm::vec2(1.0f, v), tangent);
            }

            const std::size_t edgeCount = closed ? centers.size() : centers.size() - 1;
            for (std::size_t index = 0; index < edgeCount; ++index)
            {
                const std::size_t nextIndex = (index + 1) % centers.size();
                const auto base = static_cast<unsigned int>(index * 4);
                const auto nextBase = static_cast<unsigned int>(nextIndex * 4);
                AddQuad(meshData, base + 0, base + 1, nextBase + 0, nextBase + 1);
                if (includeSideFaces && bottomOffset > 0.0f)
                {
                    AddQuad(meshData, base + 2, base + 0, nextBase + 2, nextBase + 0);
                    AddQuad(meshData, base + 1, base + 3, nextBase + 1, nextBase + 3);
                    AddQuad(meshData, base + 3, base + 2, nextBase + 3, nextBase + 2);
                }
            }

            render::MeshConfig config;
            config.data = std::move(meshData);
            config.submeshes.push_back(render::Submesh{
                .indexOffset = 0,
                .indexCount = static_cast<uint32_t>(config.data.indices.size()),
                .materialIndex = 0,
                .name = "Spline Track",
            });

            return std::unique_ptr<render::Mesh>(render::Mesh::CreateInitialized(config));
        }

    }

    SplineComponent::SplineComponent(const SplineComponentConfig &config)
        : m_points(config.points),
          m_width(std::max(config.width, 0.05f)),
          m_thickness(std::max(config.thickness, 0.0f)),
          m_samplesPerSegment(std::max(1, static_cast<int>(std::round(config.samplesPerSegment)))),
          m_collisionSamplesPerSegment(std::max(1, static_cast<int>(std::round(config.collisionSamplesPerSegment)))),
          m_uvMetersPerTile(std::max(config.uvMetersPerTile, 0.01f)),
          m_closed(config.closed),
          m_generateMesh(config.generateMesh),
          m_generateCollision(config.generateCollision),
          m_material(config.material),
          m_materialAssetReference(config.materialAssetReference)
    {
        EnsureDefaultPoints();
        RebuildMaterialFromReference();
    }

    void SplineComponent::EnsureDefaultPoints()
    {
        if (!m_points.empty())
        {
            return;
        }

        m_points = {
            {{-20.0f, 0.0f, -20.0f}},
            {{20.0f, 0.0f, -20.0f}},
            {{24.0f, 0.0f, 16.0f}},
            {{-18.0f, 0.0f, 20.0f}},
        };
    }

    void SplineComponent::Update(float deltaTime)
    {
        (void)deltaTime;
        if (m_dirty)
        {
            Rebuild();
        }
    }

    void SplineComponent::SetPoints(std::vector<SplineControlPoint> points)
    {
        m_points = std::move(points);
        EnsureDefaultPoints();
        MarkDirty();
    }

    void SplineComponent::AddPoint(const glm::vec3 &position)
    {
        m_points.push_back({position});
        MarkDirty();
    }

    void SplineComponent::InsertPoint(std::size_t index, const glm::vec3 &position)
    {
        index = std::min(index, m_points.size());
        m_points.insert(m_points.begin() + static_cast<std::ptrdiff_t>(index), {position});
        MarkDirty();
    }

    void SplineComponent::RemovePoint(std::size_t index)
    {
        if (index >= m_points.size() || m_points.size() <= 2)
        {
            return;
        }

        m_points.erase(m_points.begin() + static_cast<std::ptrdiff_t>(index));
        MarkDirty();
    }

    void SplineComponent::SetPointPosition(std::size_t index, const glm::vec3 &position)
    {
        if (index >= m_points.size())
        {
            return;
        }

        m_points[index].position = position;
        MarkDirty();
    }

    void SplineComponent::SetWidth(float width)
    {
        m_width = std::max(width, 0.05f);
        MarkDirty();
    }

    void SplineComponent::SetThickness(float thickness)
    {
        m_thickness = std::max(thickness, 0.0f);
        MarkDirty();
    }

    void SplineComponent::SetSamplesPerSegment(int samplesPerSegment)
    {
        m_samplesPerSegment = std::clamp(samplesPerSegment, 1, 128);
        MarkDirty();
    }

    void SplineComponent::SetCollisionSamplesPerSegment(int collisionSamplesPerSegment)
    {
        m_collisionSamplesPerSegment = std::clamp(collisionSamplesPerSegment, 1, 128);
        MarkDirty();
    }

    void SplineComponent::SetUvMetersPerTile(float uvMetersPerTile)
    {
        m_uvMetersPerTile = std::max(uvMetersPerTile, 0.01f);
        MarkDirty();
    }

    void SplineComponent::SetClosed(bool closed)
    {
        m_closed = closed;
        MarkDirty();
    }

    void SplineComponent::SetGenerateMesh(bool generateMesh)
    {
        m_generateMesh = generateMesh;
        MarkDirty();
    }

    void SplineComponent::SetGenerateCollision(bool generateCollision)
    {
        m_generateCollision = generateCollision;
        MarkDirty();
    }

    void SplineComponent::SetMaterial(render::Material *material)
    {
        m_material = material;
        ApplyGeneratedComponents();
    }

    void SplineComponent::SetMaterialAssetReference(const std::string &materialAssetReference)
    {
        m_materialAssetReference = materialAssetReference;
        RebuildMaterialFromReference();
        ApplyGeneratedComponents();
    }

    void SplineComponent::RebuildMaterialFromReference()
    {
        if (m_materialAssetReference.empty())
        {
            return;
        }

        m_material = core::Engine::GetInstance().GetAssetManager().LoadMaterialAsset(m_materialAssetReference);
    }

    void SplineComponent::Rebuild()
    {
        m_dirty = false;
        EnsureDefaultPoints();

        if (m_points.size() < 2)
        {
            m_generatedMesh.reset();
            m_generatedCollisionMesh.reset();
            m_collisionPathPoints.clear();
            ApplyGeneratedComponents();
            return;
        }

        if (m_generateMesh)
        {
            const auto renderCenters = BuildSplineCenters(m_points, m_closed, m_samplesPerSegment);
            m_generatedMesh = BuildSplineMesh(renderCenters, m_width, m_thickness, m_uvMetersPerTile, m_closed, true);
        }
        else
        {
            m_generatedMesh.reset();
        }

        if (m_generateCollision)
        {
            m_collisionPathPoints = BuildSplineCenters(m_points, m_closed, std::min(m_samplesPerSegment, m_collisionSamplesPerSegment));
            m_generatedCollisionMesh = BuildSplineMesh(m_collisionPathPoints,
                                                       m_width,
                                                       m_thickness,
                                                       m_uvMetersPerTile,
                                                       m_closed,
                                                       false);
        }
        else
        {
            m_generatedCollisionMesh.reset();
            m_collisionPathPoints.clear();
        }
        ApplyGeneratedComponents();
    }

    void SplineComponent::ApplyGeneratedComponents()
    {
        auto *owner = GetOwner();
        if (!owner)
        {
            return;
        }

        auto *meshComponent = owner->GetComponent<MeshComponent>();
        if (!meshComponent && m_generateMesh)
        {
            meshComponent = owner->CreateComponent<MeshComponent>(MeshComponentConfig{});
        }

        if (meshComponent)
        {
            meshComponent->SetMesh(m_generateMesh ? m_generatedMesh.get() : nullptr);
            meshComponent->SetMaterial(m_material);
            if (!m_materialAssetReference.empty())
            {
                meshComponent->SetMaterialAssetForMaterialSlot(0, m_materialAssetReference);
            }
            meshComponent->SetStatic(true);
        }

        auto *collider = owner->GetComponent<ColliderComponent>();
        if (!collider && m_generateCollision)
        {
            collider = owner->CreateComponent<ColliderComponent>(ColliderComponentConfig{.shape = ColliderShape::Mesh});
        }
        if (collider && m_generateCollision)
        {
            collider->SetShape(ColliderShape::Mesh);
            collider->SetTrigger(false);
        }
    }

    std::vector<Property> SplineComponent::Serialize() const
    {
        std::vector<Property> properties = {
            {"Width", PropertyType::Float, std::to_string(m_width)},
            {"Thickness", PropertyType::Float, std::to_string(m_thickness)},
            {"SamplesPerSegment", PropertyType::Int, std::to_string(m_samplesPerSegment)},
            {"CollisionSamplesPerSegment", PropertyType::Int, std::to_string(m_collisionSamplesPerSegment)},
            {"UvMetersPerTile", PropertyType::Float, std::to_string(m_uvMetersPerTile)},
            {"Closed", PropertyType::Bool, m_closed ? "true" : "false"},
            {"GenerateMesh", PropertyType::Bool, m_generateMesh ? "true" : "false"},
            {"GenerateCollision", PropertyType::Bool, m_generateCollision ? "true" : "false"},
            {"MaterialAsset", PropertyType::String, m_materialAssetReference},
            {"PointCount", PropertyType::Int, std::to_string(m_points.size())},
        };

        for (std::size_t index = 0; index < m_points.size(); ++index)
        {
            properties.push_back({"Points." + std::to_string(index), PropertyType::Vec3, SerializeVec3(m_points[index].position)});
        }

        return properties;
    }

    void SplineComponent::Deserialize(const std::vector<Property> &properties)
    {
        std::vector<SplineControlPoint> deserializedPoints;
        int pointCount = -1;

        for (const auto &property : properties)
        {
            if (property.name == "Width")
                m_width = std::max(std::stof(property.value), 0.05f);
            else if (property.name == "Thickness")
                m_thickness = std::max(std::stof(property.value), 0.0f);
            else if (property.name == "SamplesPerSegment")
                m_samplesPerSegment = std::clamp(std::stoi(property.value), 1, 128);
            else if (property.name == "CollisionSamplesPerSegment")
                m_collisionSamplesPerSegment = std::clamp(std::stoi(property.value), 1, 128);
            else if (property.name == "UvMetersPerTile")
                m_uvMetersPerTile = std::max(std::stof(property.value), 0.01f);
            else if (property.name == "Closed")
                m_closed = ParseBool(property.value);
            else if (property.name == "GenerateMesh")
                m_generateMesh = ParseBool(property.value);
            else if (property.name == "GenerateCollision")
                m_generateCollision = ParseBool(property.value);
            else if (property.name == "MaterialAsset")
                m_materialAssetReference = property.value;
            else if (property.name == "PointCount")
                pointCount = std::max(std::stoi(property.value), 0);
            else if (property.name.rfind("Points.", 0) == 0)
            {
                const auto index = static_cast<std::size_t>(std::stoul(property.name.substr(7)));
                if (index >= deserializedPoints.size())
                {
                    deserializedPoints.resize(index + 1);
                }
                deserializedPoints[index].position = ParseVec3(property.value);
            }
        }

        if (pointCount >= 0 && deserializedPoints.size() > static_cast<std::size_t>(pointCount))
        {
            deserializedPoints.resize(static_cast<std::size_t>(pointCount));
        }
        if (!deserializedPoints.empty())
        {
            m_points = std::move(deserializedPoints);
        }

        EnsureDefaultPoints();
        RebuildMaterialFromReference();
        Rebuild();
    }
}
