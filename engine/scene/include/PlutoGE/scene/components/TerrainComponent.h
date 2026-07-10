#pragma once

#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/scene/components/Component.h"

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace PlutoGE::render
{
    class Material;
}

namespace PlutoGE::scene
{
    enum class TerrainPaintMode
    {
        Raise,
        Lower,
        Smooth,
        Flatten,
    };

    struct TerrainComponentConfig
    {
        int width = 65;
        int depth = 65;
        float cellSize = 1.0f;
        float heightScale = 10.0f;
        int chunkSize = 32;
        int lodCount = 4;
        render::Material *material = nullptr;
    };

    class TerrainComponent : public TypedComponent<TerrainComponent>
    {
    public:
        explicit TerrainComponent(const TerrainComponentConfig &config = {});
        ~TerrainComponent() override = default;

        void Update(float deltaTime) override;
        void SubmitRenderCommands();

        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        bool LoadHeightMap(const std::string &filePath);
        bool SaveHeightMap(const std::string &filePath) const;
        bool PaintAtWorldPosition(const glm::vec3 &worldPosition, float deltaTime);
        bool Raycast(const glm::vec3 &worldOrigin, const glm::vec3 &worldDirection, glm::vec3 &worldHitPoint) const;
        const std::vector<float> &GetHeightSamples() const { return m_heights; }

        void SetSize(int width, int depth);
        int GetWidth() const { return m_width; }
        int GetDepth() const { return m_depth; }
        void SetCellSize(float cellSize);
        float GetCellSize() const { return m_cellSize; }
        void SetHeightScale(float heightScale);
        float GetHeightScale() const { return m_heightScale; }
        void SetSurfaceSmoothing(float smoothing);
        float GetSurfaceSmoothing() const { return m_surfaceSmoothing; }
        float GetHeightAtLocalPosition(float x, float z) const;
        void SetChunkSize(int chunkSize);
        int GetChunkSize() const { return m_chunkSize; }
        void SetLodCount(int lodCount);
        int GetLodCount() const { return m_lodCount; }

        void SetMaterial(render::Material *material);
        render::Material *GetMaterial() const { return m_material; }
        void SetMaterialAssetReference(const std::string &materialAssetReference);
        const std::string &GetMaterialAssetReference() const { return m_materialAssetReference; }
        void SetPaintedAlbedoPath(const std::string &path);
        const std::string &GetPaintedAlbedoPath() const { return m_paintedAlbedoPath; }
        const std::string &GetHeightMapPath() const { return m_heightMapPath; }

        void SetPaintEnabled(bool enabled) { m_paintEnabled = enabled; }
        bool IsPaintEnabled() const { return m_paintEnabled; }
        void SetPaintMode(TerrainPaintMode mode) { m_paintMode = mode; }
        TerrainPaintMode GetPaintMode() const { return m_paintMode; }
        void SetBrushRadius(float radius) { m_brushRadius = glm::max(radius, 0.05f); }
        float GetBrushRadius() const { return m_brushRadius; }
        void SetBrushStrength(float strength) { m_brushStrength = glm::max(strength, 0.0f); }
        float GetBrushStrength() const { return m_brushStrength; }
        void SetFlattenHeight(float height) { m_flattenHeight = height; }
        float GetFlattenHeight() const { return m_flattenHeight; }

    private:
        float GetHeightSample(int x, int z) const;
        void SetHeightSample(int x, int z, float height);
        float SampleHeight(float x, float z) const;
        glm::vec3 ComputeNormal(int x, int z) const;
        void EnsureHeightStorage();
        void MarkMeshDirty();
        void RebuildMesh();
        void RebuildMaterialFromReference();

        int m_width = 65;
        int m_depth = 65;
        float m_cellSize = 1.0f;
        float m_heightScale = 10.0f;
        float m_surfaceSmoothing = 0.35f;
        int m_chunkSize = 32;
        int m_lodCount = 4;
        std::vector<float> m_heights;

        std::unique_ptr<render::Mesh> m_mesh;
        render::Material *m_material = nullptr;
        std::string m_materialAssetReference;
        std::string m_paintedAlbedoPath;
        std::string m_heightMapPath;
        bool m_meshDirty = true;

        bool m_paintEnabled = false;
        TerrainPaintMode m_paintMode = TerrainPaintMode::Raise;
        float m_brushRadius = 3.0f;
        float m_brushStrength = 2.0f;
        float m_flattenHeight = 0.0f;

        glm::mat4 m_previousModelMatrix = glm::mat4(1.0f);
        bool m_hasPreviousModelMatrix = false;
    };
}
