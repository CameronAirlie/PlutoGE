#pragma once

#include "PlutoGE/scene/components/Component.h"

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace PlutoGE::render
{
    class Material;
    class Mesh;
}

namespace PlutoGE::scene
{
    struct SplineControlPoint
    {
        glm::vec3 position{0.0f};
        glm::vec3 rotation{0.0f};
    };

    struct SplineComponentConfig
    {
        std::vector<SplineControlPoint> points;
        float width = 8.0f;
        float thickness = 0.25f;
        float samplesPerSegment = 12.0f;
        float maxChordError = 0.1f;
        float maxTangentAngleDegrees = 5.0f;
        float uvMetersPerTile = 6.0f;
        bool closed = true;
        bool generateMesh = true;
        bool generateCollision = true;
        render::Material *material = nullptr;
        std::string materialAssetReference;
    };

    class SplineComponent : public TypedComponent<SplineComponent>
    {
    public:
        explicit SplineComponent(const SplineComponentConfig &config = {});
        ~SplineComponent() override = default;

        void Update(float deltaTime) override;
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        const std::vector<SplineControlPoint> &GetPoints() const { return m_points; }
        void SetPoints(std::vector<SplineControlPoint> points);
        void AddPoint(const glm::vec3 &position);
        void RemovePoint(std::size_t index);
        void SetPointPosition(std::size_t index, const glm::vec3 &position);
        void SetPointRotation(std::size_t index, const glm::vec3 &rotation);

        float GetWidth() const { return m_width; }
        void SetWidth(float width);
        float GetThickness() const { return m_thickness; }
        void SetThickness(float thickness);
        int GetSamplesPerSegment() const { return m_samplesPerSegment; }
        void SetSamplesPerSegment(int samplesPerSegment);
        float GetMaxChordError() const { return m_maxChordError; }
        void SetMaxChordError(float maxChordError);
        float GetMaxTangentAngleDegrees() const { return m_maxTangentAngleDegrees; }
        void SetMaxTangentAngleDegrees(float maxTangentAngleDegrees);
        float GetUvMetersPerTile() const { return m_uvMetersPerTile; }
        void SetUvMetersPerTile(float uvMetersPerTile);
        bool IsClosed() const { return m_closed; }
        void SetClosed(bool closed);
        bool ShouldGenerateMesh() const { return m_generateMesh; }
        void SetGenerateMesh(bool generateMesh);
        bool ShouldGenerateCollision() const { return m_generateCollision; }
        void SetGenerateCollision(bool generateCollision);
        void SetMaterial(render::Material *material);
        render::Material *GetMaterial() const { return m_material; }
        void SetMaterialAssetReference(const std::string &materialAssetReference);
        const std::string &GetMaterialAssetReference() const { return m_materialAssetReference; }
        render::Mesh *GetGeneratedMesh() const { return m_generatedMesh.get(); }

        void Rebuild();

    private:
        void MarkDirty() { m_dirty = true; }
        void EnsureDefaultPoints();
        void ApplyGeneratedComponents();
        void RebuildMaterialFromReference();

        std::vector<SplineControlPoint> m_points;
        float m_width = 8.0f;
        float m_thickness = 0.25f;
        int m_samplesPerSegment = 12;
        float m_maxChordError = 0.1f;
        float m_maxTangentAngleDegrees = 5.0f;
        float m_uvMetersPerTile = 6.0f;
        bool m_closed = true;
        bool m_generateMesh = true;
        bool m_generateCollision = true;
        render::Material *m_material = nullptr;
        std::string m_materialAssetReference;
        std::unique_ptr<render::Mesh> m_generatedMesh;
        bool m_dirty = true;
    };
}
