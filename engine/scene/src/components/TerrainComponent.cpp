#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/render/Texture.h"

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"

#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace PlutoGE::scene
{
    namespace
    {
        constexpr float kRayEpsilon = 0.0001f;
        constexpr float kTriangleDeterminantEpsilon = 0.00000001f;
        constexpr float kTriangleDistanceEpsilon = 0.000001f;

        TerrainPaintMode ParsePaintMode(const std::string &value)
        {
            if (value == "Lower" || value == "lower" || value == "1")
            {
                return TerrainPaintMode::Lower;
            }
            if (value == "Smooth" || value == "smooth" || value == "2")
            {
                return TerrainPaintMode::Smooth;
            }
            if (value == "Flatten" || value == "flatten" || value == "3")
            {
                return TerrainPaintMode::Flatten;
            }
            return TerrainPaintMode::Raise;
        }

        std::string SerializeHeightSamples(const std::vector<float> &heights)
        {
            std::ostringstream output;
            output << std::setprecision(9);
            for (std::size_t index = 0; index < heights.size(); ++index)
            {
                if (index > 0)
                {
                    output << ',';
                }
                output << heights[index];
            }
            return output.str();
        }

        bool DeserializeHeightSamples(const std::string &value, std::size_t expectedCount, std::vector<float> &heights)
        {
            if (expectedCount == 0)
            {
                return false;
            }

            std::vector<float> parsed;
            parsed.reserve(expectedCount);
            std::size_t begin = 0;
            while (begin <= value.size())
            {
                const std::size_t end = value.find(',', begin);
                const std::string token = value.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
                if (!token.empty())
                {
                    try
                    {
                        parsed.push_back(std::stof(token));
                    }
                    catch (...)
                    {
                        return false;
                    }
                }

                if (end == std::string::npos)
                {
                    break;
                }
                begin = end + 1;
            }

            if (parsed.size() != expectedCount)
            {
                return false;
            }

            for (auto &height : parsed)
            {
                if (!std::isfinite(height))
                {
                    height = 0.0f;
                }
            }
            heights = std::move(parsed);
            return true;
        }

        render::MeshBounds ComputeWorldBounds(const render::Mesh &mesh, std::size_t submeshIndex, const glm::mat4 &modelMatrix)
        {
            const auto &bounds = submeshIndex < mesh.GetSubmeshCount() ? mesh.GetSubmesh(submeshIndex).bounds : mesh.GetBounds();
            const glm::vec3 worldCenter = glm::vec3(modelMatrix * glm::vec4(bounds.center, 1.0f));
            const float scaleX = glm::length(glm::vec3(modelMatrix[0]));
            const float scaleY = glm::length(glm::vec3(modelMatrix[1]));
            const float scaleZ = glm::length(glm::vec3(modelMatrix[2]));
            return render::MeshBounds{
                .center = worldCenter,
                .radius = bounds.radius * std::max(scaleX, std::max(scaleY, scaleZ)),
            };
        }

        bool IntersectTriangle(const glm::vec3 &origin,
                               const glm::vec3 &direction,
                               const glm::vec3 &v0,
                               const glm::vec3 &v1,
                               const glm::vec3 &v2,
                               float &distance)
        {
            const glm::vec3 edge1 = v1 - v0;
            const glm::vec3 edge2 = v2 - v0;
            const glm::vec3 p = glm::cross(direction, edge2);
            const float determinant = glm::dot(edge1, p);
            if (std::abs(determinant) <= kTriangleDeterminantEpsilon)
            {
                return false;
            }

            const float inverseDeterminant = 1.0f / determinant;
            const glm::vec3 t = origin - v0;
            const float u = glm::dot(t, p) * inverseDeterminant;
            if (u < 0.0f || u > 1.0f)
            {
                return false;
            }

            const glm::vec3 q = glm::cross(t, edge1);
            const float v = glm::dot(direction, q) * inverseDeterminant;
            if (v < 0.0f || u + v > 1.0f)
            {
                return false;
            }

            const float hitDistance = glm::dot(edge2, q) * inverseDeterminant;
            if (hitDistance <= kTriangleDistanceEpsilon)
            {
                return false;
            }

            distance = hitDistance;
            return true;
        }
    }

    TerrainComponent::TerrainComponent(const TerrainComponentConfig &config)
        : m_width(std::max(config.width, 2)),
          m_depth(std::max(config.depth, 2)),
          m_cellSize(std::max(config.cellSize, 0.01f)),
          m_heightScale(std::max(config.heightScale, 0.01f)),
          m_chunkSize(std::max(config.chunkSize, 2)),
          m_lodCount(glm::clamp(config.lodCount, 1, 6)),
          m_material(config.material)
    {
        EnsureHeightStorage();
    }

    void TerrainComponent::Update(float deltaTime)
    {
        (void)deltaTime;
    }

    void TerrainComponent::SubmitRenderCommands()
    {
        if (m_meshDirty)
        {
            RebuildMesh();
        }

        if (!m_mesh || !m_material)
        {
            return;
        }

        auto *entity = GetOwner();
        if (!entity)
        {
            return;
        }

        auto &renderer = core::Engine::GetInstance().GetRenderer();
        const glm::mat4 modelMatrix = entity->GetWorldTransform();
        for (std::size_t submeshIndex = 0; submeshIndex < m_mesh->GetSubmeshCount(); ++submeshIndex)
        {
            render::RenderCommand command;
            command.model = modelMatrix;
            command.previousModel = m_hasPreviousModelMatrix ? m_previousModelMatrix : modelMatrix;
            command.material = m_material;
            command.mesh = m_mesh.get();
            command.shader = m_material->GetShader();
            command.worldBounds = ComputeWorldBounds(*m_mesh, submeshIndex, modelMatrix);
            command.previousWorldBounds = ComputeWorldBounds(*m_mesh, submeshIndex, command.previousModel);
            command.submeshIndex = static_cast<uint32_t>(submeshIndex);
            command.isStatic = false;
            command.usePrimaryUvForLightmap = true;
            renderer.SubmitRenderCommand(command);
        }

        m_previousModelMatrix = modelMatrix;
        m_hasPreviousModelMatrix = true;
    }

    std::vector<Property> TerrainComponent::Serialize() const
    {
        return {
            {"Width", PropertyType::Int, std::to_string(m_width)},
            {"Depth", PropertyType::Int, std::to_string(m_depth)},
            {"CellSize", PropertyType::Float, std::to_string(m_cellSize)},
            {"HeightScale", PropertyType::Float, std::to_string(m_heightScale)},
            {"SurfaceSmoothing", PropertyType::Float, std::to_string(m_surfaceSmoothing)},
            {"ChunkSize", PropertyType::Int, std::to_string(m_chunkSize)},
            {"LodCount", PropertyType::Int, std::to_string(m_lodCount)},
            {"HeightMap", PropertyType::String, m_heightMapPath},
            {"HeightSamples", PropertyType::String, SerializeHeightSamples(m_heights)},
            {"MaterialAsset", PropertyType::String, m_materialAssetReference},
            {"PaintedAlbedo", PropertyType::String, m_paintedAlbedoPath},
            {"PaintEnabled", PropertyType::Bool, m_paintEnabled ? "true" : "false"},
            {"PaintMode", PropertyType::String, std::to_string(static_cast<int>(m_paintMode))},
            {"BrushRadius", PropertyType::Float, std::to_string(m_brushRadius)},
            {"BrushStrength", PropertyType::Float, std::to_string(m_brushStrength)},
            {"FlattenHeight", PropertyType::Float, std::to_string(m_flattenHeight)},
        };
    }

    void TerrainComponent::Deserialize(const std::vector<Property> &properties)
    {
        std::string heightMapPath = m_heightMapPath;
        std::string materialAssetReference = m_materialAssetReference;
        std::string paintedAlbedoPath = m_paintedAlbedoPath;
        std::string serializedHeightSamples;
        for (const auto &property : properties)
        {
            if (property.name == "Width")
                m_width = std::max(2, std::stoi(property.value));
            else if (property.name == "Depth")
                m_depth = std::max(2, std::stoi(property.value));
            else if (property.name == "CellSize")
                m_cellSize = std::max(0.01f, std::stof(property.value));
            else if (property.name == "HeightScale")
                m_heightScale = std::max(0.01f, std::stof(property.value));
            else if (property.name == "SurfaceSmoothing")
                m_surfaceSmoothing = glm::clamp(std::stof(property.value), 0.0f, 1.0f);
            else if (property.name == "ChunkSize")
                m_chunkSize = std::max(2, std::stoi(property.value));
            else if (property.name == "LodCount")
                m_lodCount = glm::clamp(std::stoi(property.value), 1, 6);
            else if (property.name == "HeightMap")
                heightMapPath = property.value;
            else if (property.name == "HeightSamples")
                serializedHeightSamples = property.value;
            else if (property.name == "MaterialAsset")
                materialAssetReference = property.value;
            else if (property.name == "PaintedAlbedo")
                paintedAlbedoPath = property.value;
            else if (property.name == "PaintEnabled")
                m_paintEnabled = property.value == "true" || property.value == "1";
            else if (property.name == "PaintMode")
                m_paintMode = ParsePaintMode(property.value);
            else if (property.name == "BrushRadius")
                m_brushRadius = std::max(0.05f, std::stof(property.value));
            else if (property.name == "BrushStrength")
                m_brushStrength = std::max(0.0f, std::stof(property.value));
            else if (property.name == "FlattenHeight")
                m_flattenHeight = std::stof(property.value);
        }

        EnsureHeightStorage();
        const std::size_t expectedHeightSampleCount = static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_depth);
        if (!serializedHeightSamples.empty() &&
            DeserializeHeightSamples(serializedHeightSamples, expectedHeightSampleCount, m_heights))
        {
            m_heightMapPath = heightMapPath;
            MarkMeshDirty();
        }
        else if (!heightMapPath.empty() && heightMapPath != m_heightMapPath)
        {
            LoadHeightMap(heightMapPath);
        }
        else
        {
            MarkMeshDirty();
        }

        m_materialAssetReference = materialAssetReference;
        RebuildMaterialFromReference();
        if (!paintedAlbedoPath.empty())
            SetPaintedAlbedoPath(paintedAlbedoPath);
    }

    bool TerrainComponent::LoadHeightMap(const std::string &filePath)
    {
        std::string resolvedPath = core::Engine::GetInstance().GetAssetManager().ResolveAssetPath(filePath);
        if (resolvedPath.empty())
        {
            resolvedPath = filePath;
        }

        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char *pixels = stbi_load(resolvedPath.c_str(), &width, &height, &channels, 1);
        if (!pixels || width <= 1 || height <= 1)
        {
            if (pixels)
            {
                stbi_image_free(pixels);
            }
            return false;
        }

        m_width = width;
        m_depth = height;
        m_heights.resize(static_cast<std::size_t>(m_width * m_depth));
        for (int z = 0; z < m_depth; ++z)
        {
            for (int x = 0; x < m_width; ++x)
            {
                const std::size_t src = static_cast<std::size_t>((m_depth - 1 - z) * m_width + x);
                SetHeightSample(x, z, static_cast<float>(pixels[src]) / 255.0f * m_heightScale);
            }
        }

        stbi_image_free(pixels);
        m_heightMapPath = filePath;
        MarkMeshDirty();
        return true;
    }

    bool TerrainComponent::SaveHeightMap(const std::string &filePath) const
    {
        std::ofstream output(filePath, std::ios::binary);
        if (!output.is_open())
        {
            return false;
        }

        output << "P5\n"
               << m_width << " " << m_depth << "\n255\n";
        for (int z = m_depth - 1; z >= 0; --z)
        {
            for (int x = 0; x < m_width; ++x)
            {
                const float normalized = glm::clamp(GetHeightSample(x, z) / std::max(m_heightScale, 0.01f), 0.0f, 1.0f);
                const unsigned char value = static_cast<unsigned char>(std::lround(normalized * 255.0f));
                output.write(reinterpret_cast<const char *>(&value), 1);
            }
        }
        return true;
    }

    bool TerrainComponent::PaintAtWorldPosition(const glm::vec3 &worldPosition, float deltaTime)
    {
        auto *entity = GetOwner();
        if (!entity || !m_paintEnabled)
        {
            return false;
        }

        const glm::mat4 inverseWorld = glm::inverse(entity->GetWorldTransform());
        const glm::vec3 local = glm::vec3(inverseWorld * glm::vec4(worldPosition, 1.0f));
        const float centerX = local.x / m_cellSize;
        const float centerZ = local.z / m_cellSize;
        const float radiusInSamples = m_brushRadius / m_cellSize;
        const int minX = std::max(0, static_cast<int>(std::floor(centerX - radiusInSamples)));
        const int maxX = std::min(m_width - 1, static_cast<int>(std::ceil(centerX + radiusInSamples)));
        const int minZ = std::max(0, static_cast<int>(std::floor(centerZ - radiusInSamples)));
        const int maxZ = std::min(m_depth - 1, static_cast<int>(std::ceil(centerZ + radiusInSamples)));
        if (minX > maxX || minZ > maxZ)
        {
            return false;
        }

        std::vector<float> sourceHeights = m_heights;
        const float signedStrength = (m_paintMode == TerrainPaintMode::Lower ? -1.0f : 1.0f) * m_brushStrength * std::max(deltaTime, 1.0f / 120.0f);
        bool changed = false;
        for (int z = minZ; z <= maxZ; ++z)
        {
            for (int x = minX; x <= maxX; ++x)
            {
                const float dx = (static_cast<float>(x) - centerX) * m_cellSize;
                const float dz = (static_cast<float>(z) - centerZ) * m_cellSize;
                const float distance = std::sqrt(dx * dx + dz * dz);
                if (distance > m_brushRadius)
                {
                    continue;
                }

                const float falloff = 1.0f - glm::smoothstep(0.0f, m_brushRadius, distance);
                const std::size_t index = static_cast<std::size_t>(z * m_width + x);
                float nextHeight = sourceHeights[index];
                if (m_paintMode == TerrainPaintMode::Smooth)
                {
                    float total = 0.0f;
                    int count = 0;
                    for (int oz = -1; oz <= 1; ++oz)
                    {
                        for (int ox = -1; ox <= 1; ++ox)
                        {
                            const int sx = glm::clamp(x + ox, 0, m_width - 1);
                            const int sz = glm::clamp(z + oz, 0, m_depth - 1);
                            total += sourceHeights[static_cast<std::size_t>(sz * m_width + sx)];
                            ++count;
                        }
                    }
                    nextHeight = glm::mix(sourceHeights[index], total / static_cast<float>(count), glm::clamp(falloff * m_brushStrength * deltaTime, 0.0f, 1.0f));
                }
                else if (m_paintMode == TerrainPaintMode::Flatten)
                {
                    nextHeight = glm::mix(sourceHeights[index], m_flattenHeight, glm::clamp(falloff * m_brushStrength * deltaTime, 0.0f, 1.0f));
                }
                else
                {
                    nextHeight = sourceHeights[index] + signedStrength * falloff;
                }

                nextHeight = glm::clamp(nextHeight, 0.0f, m_heightScale);
                if (std::abs(nextHeight - m_heights[index]) > 0.00001f)
                {
                    m_heights[index] = nextHeight;
                    changed = true;
                }
            }
        }

        if (changed)
        {
            MarkMeshDirty();
        }
        return changed;
    }

    bool TerrainComponent::Raycast(const glm::vec3 &worldOrigin, const glm::vec3 &worldDirection, glm::vec3 &worldHitPoint) const
    {
        auto *entity = GetOwner();
        if (!entity || !m_mesh)
        {
            return false;
        }

        const glm::mat4 inverseWorld = glm::inverse(entity->GetWorldTransform());
        const glm::vec3 localOrigin = glm::vec3(inverseWorld * glm::vec4(worldOrigin, 1.0f));
        glm::vec3 localDirection = glm::vec3(inverseWorld * glm::vec4(worldDirection, 0.0f));
        if (glm::dot(localDirection, localDirection) <= kRayEpsilon)
        {
            return false;
        }
        localDirection = glm::normalize(localDirection);

        float selectedDistance = std::numeric_limits<float>::max();
        const auto &meshData = m_mesh->GetMeshData();
        for (std::size_t index = 0; index + 2 < meshData.indices.size(); index += 3)
        {
            const auto i0 = meshData.indices[index];
            const auto i1 = meshData.indices[index + 1];
            const auto i2 = meshData.indices[index + 2];
            if (i0 >= meshData.vertices.size() || i1 >= meshData.vertices.size() || i2 >= meshData.vertices.size())
            {
                continue;
            }

            const auto &a = meshData.vertices[i0];
            const auto &b = meshData.vertices[i1];
            const auto &c = meshData.vertices[i2];
            const glm::vec3 v0(a.position[0], a.position[1], a.position[2]);
            const glm::vec3 v1(b.position[0], b.position[1], b.position[2]);
            const glm::vec3 v2(c.position[0], c.position[1], c.position[2]);
            float distance = 0.0f;
            if (IntersectTriangle(localOrigin, localDirection, v0, v1, v2, distance) && distance < selectedDistance)
            {
                selectedDistance = distance;
            }
        }

        if (selectedDistance == std::numeric_limits<float>::max())
        {
            return false;
        }

        const glm::vec3 localHit = localOrigin + localDirection * selectedDistance;
        worldHitPoint = glm::vec3(entity->GetWorldTransform() * glm::vec4(localHit, 1.0f));
        return true;
    }

    void TerrainComponent::SetSize(int width, int depth)
    {
        const int oldWidth = m_width;
        const int oldDepth = m_depth;
        const std::vector<float> oldHeights = m_heights;
        m_width = std::max(2, width);
        m_depth = std::max(2, depth);
        std::vector<float> resized(static_cast<std::size_t>(m_width * m_depth), 0.0f);
        for (int z = 0; z < std::min(oldDepth, m_depth); ++z)
        {
            for (int x = 0; x < std::min(oldWidth, m_width); ++x)
            {
                resized[static_cast<std::size_t>(z * m_width + x)] = oldHeights[static_cast<std::size_t>(z * oldWidth + x)];
            }
        }
        m_heights.swap(resized);
        MarkMeshDirty();
    }

    void TerrainComponent::SetCellSize(float cellSize)
    {
        m_cellSize = std::max(0.01f, cellSize);
        MarkMeshDirty();
    }

    void TerrainComponent::SetHeightScale(float heightScale)
    {
        m_heightScale = std::max(0.01f, heightScale);
        for (auto &height : m_heights)
        {
            height = glm::clamp(height, 0.0f, m_heightScale);
        }
        MarkMeshDirty();
    }

    void TerrainComponent::SetSurfaceSmoothing(float smoothing)
    {
        m_surfaceSmoothing = glm::clamp(smoothing, 0.0f, 1.0f);
        MarkMeshDirty();
    }

    float TerrainComponent::GetHeightAtLocalPosition(float x, float z) const
    {
        return SampleHeight(x / m_cellSize, z / m_cellSize);
    }

    void TerrainComponent::SetChunkSize(int chunkSize)
    {
        m_chunkSize = std::max(2, chunkSize);
        MarkMeshDirty();
    }

    void TerrainComponent::SetLodCount(int lodCount)
    {
        m_lodCount = glm::clamp(lodCount, 1, 6);
        MarkMeshDirty();
    }

    void TerrainComponent::SetMaterial(render::Material *material)
    {
        m_material = material;
    }

    void TerrainComponent::SetMaterialAssetReference(const std::string &materialAssetReference)
    {
        if (m_materialAssetReference == materialAssetReference && m_material)
        {
            return;
        }

        m_materialAssetReference = materialAssetReference;
        RebuildMaterialFromReference();
    }

    void TerrainComponent::SetPaintedAlbedoPath(const std::string &path)
    {
        if (path.empty())
            return;
        const std::string resolvedPath = core::Engine::GetInstance().GetAssetManager().ResolveAssetPath(path);
        auto *texture = render::Texture::LoadFromFile(resolvedPath.c_str());
        if (!texture)
            return;
        auto *uniqueMaterial = m_material ? new render::Material(m_material->GetConfig()) : new render::Material();
        uniqueMaterial->SetAlbedoTexture(texture);
        m_material = uniqueMaterial;
        m_paintedAlbedoPath = path;
    }

    float TerrainComponent::GetHeightSample(int x, int z) const
    {
        x = glm::clamp(x, 0, m_width - 1);
        z = glm::clamp(z, 0, m_depth - 1);
        return m_heights.empty() ? 0.0f : m_heights[static_cast<std::size_t>(z * m_width + x)];
    }

    void TerrainComponent::SetHeightSample(int x, int z, float height)
    {
        if (x < 0 || x >= m_width || z < 0 || z >= m_depth || m_heights.empty())
        {
            return;
        }
        m_heights[static_cast<std::size_t>(z * m_width + x)] = glm::clamp(height, 0.0f, m_heightScale);
    }

    float TerrainComponent::SampleHeight(float x, float z) const
    {
        const int x0 = glm::clamp(static_cast<int>(std::floor(x)), 0, m_width - 1);
        const int z0 = glm::clamp(static_cast<int>(std::floor(z)), 0, m_depth - 1);
        const int x1 = glm::clamp(x0 + 1, 0, m_width - 1);
        const int z1 = glm::clamp(z0 + 1, 0, m_depth - 1);
        const float tx = glm::fract(x);
        const float tz = glm::fract(z);
        const float h0 = glm::mix(GetHeightSample(x0, z0), GetHeightSample(x1, z0), tx);
        const float h1 = glm::mix(GetHeightSample(x0, z1), GetHeightSample(x1, z1), tx);
        return glm::mix(h0, h1, tz);
    }

    glm::vec3 TerrainComponent::ComputeNormal(int x, int z) const
    {
        const auto smoothed = [&](int sx, int sz)
        {
            const float raw = GetHeightSample(sx, sz);
            if (m_surfaceSmoothing <= 0.0f)
            {
                return raw;
            }

            float total = 0.0f;
            float weight = 0.0f;
            for (int oz = -1; oz <= 1; ++oz)
            {
                for (int ox = -1; ox <= 1; ++ox)
                {
                    const float sampleWeight = (ox == 0 && oz == 0) ? 4.0f : (ox == 0 || oz == 0 ? 2.0f : 1.0f);
                    total += GetHeightSample(sx + ox, sz + oz) * sampleWeight;
                    weight += sampleWeight;
                }
            }
            return glm::mix(raw, total / std::max(weight, 0.0001f), m_surfaceSmoothing);
        };

        const float left = smoothed(x - 1, z);
        const float right = smoothed(x + 1, z);
        const float down = smoothed(x, z - 1);
        const float up = smoothed(x, z + 1);
        return glm::normalize(glm::vec3(left - right, 2.0f * m_cellSize, down - up));
    }

    void TerrainComponent::EnsureHeightStorage()
    {
        const std::size_t targetSize = static_cast<std::size_t>(m_width * m_depth);
        if (m_heights.size() == targetSize)
        {
            return;
        }

        m_heights.assign(targetSize, 0.0f);
    }

    void TerrainComponent::MarkMeshDirty()
    {
        m_meshDirty = true;
        if (auto *owner = GetOwner())
        {
            if (auto *scene = owner->GetScene())
            {
                scene->MarkShadowLightsDirty();
            }
        }
    }

    void TerrainComponent::RebuildMesh()
    {
        EnsureHeightStorage();

        render::MeshConfig config;
        config.data.vertices.reserve(static_cast<std::size_t>(m_width * m_depth));
        for (int z = 0; z < m_depth; ++z)
        {
            for (int x = 0; x < m_width; ++x)
            {
                const glm::vec3 normal = ComputeNormal(x, z);
                render::MeshVertexData vertex;
                float height = GetHeightSample(x, z);
                if (m_surfaceSmoothing > 0.0f)
                {
                    float total = 0.0f;
                    float weight = 0.0f;
                    for (int oz = -1; oz <= 1; ++oz)
                    {
                        for (int ox = -1; ox <= 1; ++ox)
                        {
                            const float sampleWeight = (ox == 0 && oz == 0) ? 4.0f : (ox == 0 || oz == 0 ? 2.0f : 1.0f);
                            total += GetHeightSample(x + ox, z + oz) * sampleWeight;
                            weight += sampleWeight;
                        }
                    }
                    height = glm::mix(height, total / std::max(weight, 0.0001f), m_surfaceSmoothing);
                }
                vertex.position = {static_cast<float>(x) * m_cellSize, height, static_cast<float>(z) * m_cellSize};
                vertex.normal = {normal.x, normal.y, normal.z};
                vertex.uv = {static_cast<float>(x) / static_cast<float>(std::max(1, m_width - 1)),
                             static_cast<float>(z) / static_cast<float>(std::max(1, m_depth - 1))};
                vertex.tangent = {1.0f, 0.0f, 0.0f, 1.0f};
                config.data.vertices.push_back(vertex);
            }
        }

        const auto appendQuad = [&](int x, int z, int step)
        {
            const int x1 = std::min(x + step, m_width - 1);
            const int z1 = std::min(z + step, m_depth - 1);
            if (x1 == x || z1 == z)
            {
                return;
            }

            const unsigned int i0 = static_cast<unsigned int>(z * m_width + x);
            const unsigned int i1 = static_cast<unsigned int>(z * m_width + x1);
            const unsigned int i2 = static_cast<unsigned int>(z1 * m_width + x);
            const unsigned int i3 = static_cast<unsigned int>(z1 * m_width + x1);
            config.data.indices.push_back(i0);
            config.data.indices.push_back(i2);
            config.data.indices.push_back(i1);
            config.data.indices.push_back(i1);
            config.data.indices.push_back(i2);
            config.data.indices.push_back(i3);
        };

        const int chunkQuads = std::max(2, m_chunkSize);
        for (int chunkZ = 0; chunkZ < m_depth - 1; chunkZ += chunkQuads)
        {
            for (int chunkX = 0; chunkX < m_width - 1; chunkX += chunkQuads)
            {
                render::Submesh submesh;
                submesh.materialIndex = 0;
                submesh.name = "Terrain Chunk " + std::to_string(chunkX / chunkQuads) + "," + std::to_string(chunkZ / chunkQuads);

                for (int lodIndex = 0; lodIndex < m_lodCount; ++lodIndex)
                {
                    const int step = 1 << lodIndex;
                    const uint32_t lodOffset = static_cast<uint32_t>(config.data.indices.size());
                    const int maxZ = std::min(chunkZ + chunkQuads, m_depth - 1);
                    const int maxX = std::min(chunkX + chunkQuads, m_width - 1);
                    for (int z = chunkZ; z < maxZ; z += step)
                    {
                        for (int x = chunkX; x < maxX; x += step)
                        {
                            appendQuad(x, z, step);
                        }
                    }

                    const uint32_t lodCount = static_cast<uint32_t>(config.data.indices.size()) - lodOffset;
                    submesh.lods.push_back(render::Submesh::LodRange{
                        .indexOffset = lodOffset,
                        .indexCount = lodCount,
                        .minDistanceFactor = static_cast<float>(lodIndex) * 2.0f,
                        .maxScreenRadiusPixels = lodIndex == 0 ? std::numeric_limits<float>::max() : 280.0f / static_cast<float>(1 << (lodIndex - 1)),
                    });
                    if (lodIndex == 0)
                    {
                        submesh.indexOffset = lodOffset;
                        submesh.indexCount = lodCount;
                    }
                }

                config.submeshes.push_back(std::move(submesh));
            }
        }

        m_mesh.reset(render::Mesh::FromConfig(std::move(config)));
        m_meshDirty = false;
        m_hasPreviousModelMatrix = false;
    }

    void TerrainComponent::RebuildMaterialFromReference()
    {
        if (m_materialAssetReference.empty())
        {
            if (!m_material)
            {
                m_material = core::Engine::GetInstance().GetAssetManager().LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference));
                m_materialAssetReference = std::string(assets::Project::kBuiltinDefaultShadedMaterialReference);
            }
            return;
        }

        if (auto *material = core::Engine::GetInstance().GetAssetManager().LoadMaterialAsset(m_materialAssetReference))
        {
            m_material = material;
            return;
        }

        m_material = core::Engine::GetInstance().GetAssetManager().LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference));
        if (m_material)
        {
            m_materialAssetReference = std::string(assets::Project::kBuiltinDefaultShadedMaterialReference);
        }
        else
        {
            m_materialAssetReference.clear();
        }
    }
}
