#include "PlutoGE/platform/ContentPack.h"
#include <climits>
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/scene/FastTextNumbers.h"
#include "PlutoGE/render/Texture.h"

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/CpuTrace.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/RenderSceneFrontend.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
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
            if (value == "Layer" || value == "layer" || value == "4")
            {
                return TerrainPaintMode::Layer;
            }
            if (value == "Hole" || value == "hole" || value == "5")
            {
                return TerrainPaintMode::Hole;
            }
            if (value == "Fill" || value == "fill" || value == "6")
            {
                return TerrainPaintMode::Fill;
            }
            return TerrainPaintMode::Raise;
        }

        glm::vec3 ParseVec3(const std::string &value, const glm::vec3 &fallback)
        {
            glm::vec3 result = fallback;
            return std::sscanf(value.c_str(), "%f,%f,%f", &result.x, &result.y, &result.z) == 3 ? result : fallback;
        }

        std::string FormatVec3(const glm::vec3 &value)
        {
            char buffer[96];
            std::snprintf(buffer, sizeof(buffer), "%.6f,%.6f,%.6f", value.x, value.y, value.z);
            return buffer;
        }

        // Per-layer properties ("Layer.<index>.<Field>"); map fields are asset paths (SceneSerializer).
        std::string LayerProperty(std::size_t index, const char *field)
        {
            return "Layer." + std::to_string(index) + "." + field;
        }

        std::string SerializeHeightSamples(const std::vector<float> &heights)
        {
            std::string output;
            output.reserve(heights.size() * 10);
            for (std::size_t index = 0; index < heights.size(); ++index)
            {
                if (index > 0) output.push_back(',');
                AppendNumber(output, heights[index]);
            }
            return output;
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

        // An asset reference or path as a file path (references that cannot be resolved are taken as paths).
        std::string ResolveTerrainFile(const std::string &reference)
        {
            auto resolved = core::Engine::GetInstance().GetAssetManager().ResolveAssetPath(reference);
            return resolved.empty() ? reference : resolved;
        }

        // A file's bytes, from a mounted content pack or the file system.
        bool ReadTerrainFile(const std::string &path, std::vector<std::uint8_t> &bytes)
        {
            if (content::IsMounted(path))
            {
                std::string packed;
                if (!content::ReadFile(path, packed))
                    return false;
                bytes.assign(packed.begin(), packed.end());
                return true;
            }
            std::ifstream input(path, std::ios::binary);
            if (!input)
                return false;
            bytes.assign(std::istreambuf_iterator<char>(input), {});
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
        // World tiles build on a worker and draw their previous mesh (or nothing) until the new one lands.
        PollMeshBuild();
        if (m_meshDirty)
        {
            if (m_asyncMeshBuild)
                StartMeshBuild();
            else
                RebuildMesh();
        }

        auto *material = GetRenderMaterial();
        if (!m_mesh || !material)
        {
            return;
        }

        auto *entity = GetOwner();
        if (!entity)
        {
            return;
        }

        auto &renderScene = core::Engine::GetInstance().GetRenderScene();
        const glm::mat4 modelMatrix = entity->GetWorldTransform();
        for (std::size_t submeshIndex = 0; submeshIndex < m_mesh->GetSubmeshCount(); ++submeshIndex)
        {
            render::RenderCommand command;
            command.ownerEntity = entity->GetID();
            command.model = modelMatrix;
            command.previousModel = m_hasPreviousModelMatrix ? m_previousModelMatrix : modelMatrix;
            command.material = material;
            command.mesh = m_mesh.get();
            command.worldBounds = ComputeWorldBounds(*m_mesh, submeshIndex, modelMatrix);
            command.previousWorldBounds = ComputeWorldBounds(*m_mesh, submeshIndex, command.previousModel);
            command.submeshIndex = static_cast<uint32_t>(submeshIndex);
            command.isStatic = m_isStatic;
            command.terrainGeomorph = true;
            renderScene.SubmitRenderCommand(command);
        }

        m_previousModelMatrix = modelMatrix;
        m_hasPreviousModelMatrix = true;
    }

    render::Mesh *TerrainComponent::GetMeshForBaking()
    {
        FinishMeshBuild();
        return m_mesh.get();
    }

    std::vector<Property> TerrainComponent::Serialize() const
    {
        return MaterializeSharedProperties(SerializeShared());
    }

    std::vector<Property> TerrainComponent::SerializeForInspector() const
    {
        return SerializeProperties(HeavyDetail::Omit);
    }

    std::vector<Property> TerrainComponent::SerializeShared() const
    {
        return SerializeProperties(HeavyDetail::Shared);
    }

    std::vector<Property> TerrainComponent::SerializeProperties(HeavyDetail detail) const
    {
        std::vector<Property> properties{
            {"Width", PropertyType::Int, std::to_string(m_width)},
            {"Depth", PropertyType::Int, std::to_string(m_depth)},
            {"CellSize", PropertyType::Float, std::to_string(m_cellSize)},
            {"HeightScale", PropertyType::Float, std::to_string(m_heightScale)},
            {"SurfaceSmoothing", PropertyType::Float, std::to_string(m_surfaceSmoothing)},
            {"ChunkSize", PropertyType::Int, std::to_string(m_chunkSize)},
            {"LodCount", PropertyType::Int, std::to_string(m_lodCount)},
            {"LodDistanceScale", PropertyType::Float, std::to_string(m_lodDistanceScale)},
            {"SkirtDepth", PropertyType::Float, std::to_string(m_skirtDepth)},
            {"Static", PropertyType::Bool, m_isStatic ? "true" : "false"},
            {"HeightMap", PropertyType::String, m_heightMapPath},
            {"HeightData", PropertyType::String, m_heightDataPath},
            {"MaterialAsset", PropertyType::String, m_materialAssetReference},
            {"PaintedAlbedo", PropertyType::String, m_paintedAlbedoPath},
        };
        if (detail == HeavyDetail::Shared)
        {
            const auto materialAsset = std::find_if(properties.begin(), properties.end(),
                                                    [](const Property &property) { return property.name == "MaterialAsset"; });
            // Always written (empty without holes): a missing HoleMask keeps the current holes, so undoing back to
            // a state without holes must say so.
            Property holes{"HoleMask", PropertyType::String};
            holes.sharedValue = m_holesText.Get(m_holesRevision, [this] { return m_holes.Encode(); });
            auto position = properties.insert(materialAsset, std::move(holes));
            // Heights kept in a height file are not part of the scene. Otherwise they stay in their long-standing
            // position, so scene files diff cleanly.
            if (m_heightDataPath.empty())
            {
                Property samples{"HeightSamples", PropertyType::String};
                samples.sharedValue = m_heightsText.Get(m_heightsRevision, [this] { return SerializeHeightSamples(m_heights); });
                properties.insert(position, std::move(samples));
            }
        }
        properties.push_back({"HeightBlend", PropertyType::Float, std::to_string(m_layers.GetHeightBlend())});
        properties.push_back({"DetailDistance", PropertyType::Float, std::to_string(m_layers.GetDetailDistance())});
        properties.push_back({"WeightResolution", PropertyType::Int, std::to_string(m_layers.GetWeights().GetResolution())});
        for (std::size_t plane = 0; plane < m_layers.GetWeightMapPaths().size(); ++plane)
            properties.push_back({"WeightMap" + std::to_string(plane), PropertyType::String, m_layers.GetWeightMapPaths()[plane]});
        const auto &layers = m_layers.GetLayers();
        properties.push_back({"LayerCount", PropertyType::Int, std::to_string(layers.size())});
        for (std::size_t index = 0; index < layers.size(); ++index)
        {
            const auto &layer = layers[index];
            properties.push_back({LayerProperty(index, "Name"), PropertyType::String, layer.name});
            properties.push_back({LayerProperty(index, "AlbedoMap"), PropertyType::String, layer.albedoMap});
            properties.push_back({LayerProperty(index, "NormalMap"), PropertyType::String, layer.normalMap});
            properties.push_back({LayerProperty(index, "RoughnessMap"), PropertyType::String, layer.roughnessMap});
            properties.push_back({LayerProperty(index, "HeightMap"), PropertyType::String, layer.heightMap});
            properties.push_back({LayerProperty(index, "Tint"), PropertyType::Vec3, FormatVec3(layer.tint)});
            properties.push_back({LayerProperty(index, "TileSize"), PropertyType::Float, std::to_string(layer.tileSize)});
            properties.push_back({LayerProperty(index, "NormalStrength"), PropertyType::Float, std::to_string(layer.normalStrength)});
            properties.push_back({LayerProperty(index, "Roughness"), PropertyType::Float, std::to_string(layer.roughness)});
            properties.push_back({LayerProperty(index, "Metallic"), PropertyType::Float, std::to_string(layer.metallic)});
            properties.push_back({LayerProperty(index, "AntiTiling"), PropertyType::Bool, layer.antiTiling ? "true" : "false"});
        }
        return properties;
    }

    void TerrainComponent::Deserialize(const std::vector<Property> &properties)
    {
        const int previousWidth = m_width;
        const int previousDepth = m_depth;
        std::string heightMapPath = m_heightMapPath;
        std::optional<std::string> heightDataPath;
        std::string materialAssetReference = m_materialAssetReference;
        std::string paintedAlbedoPath = m_paintedAlbedoPath;
        std::string serializedHeightSamples;
        std::optional<std::string> serializedHoles;
        // Missing layer properties keep the current layers: partial lists (the inspector's terrain section, script
        // property sets) carry none of them and must not clear the layers.
        std::vector<TerrainLayer> layers = m_layers.GetLayers();
        std::size_t layerCount = 0;
        bool hasLayerCount = false;
        int weightResolution = m_layers.GetWeights().GetResolution();
        float heightBlend = m_layers.GetHeightBlend();
        float detailDistance = m_layers.GetDetailDistance();
        auto weightMapPaths = m_layers.GetWeightMapPaths();
        const auto layerAt = [&](const std::string &name, std::string &field) -> TerrainLayer * {
            // "Layer.<index>.<Field>"
            const auto dot = name.find('.', 6);
            if (name.rfind("Layer.", 0) != 0 || dot == std::string::npos)
                return nullptr;
            const auto index = static_cast<std::size_t>(std::strtoul(name.c_str() + 6, nullptr, 10));
            if (index >= TerrainLayerStack::kMaxLayers)
                return nullptr;
            if (layers.size() <= index)
                layers.resize(index + 1);
            field = name.substr(dot + 1);
            return &layers[index];
        };
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
            else if (property.name == "LodDistanceScale")
                m_lodDistanceScale = glm::clamp(std::stof(property.value), 0.25f, 16.0f);
            else if (property.name == "SkirtDepth")
                m_skirtDepth = glm::clamp(std::stof(property.value), 0.0f, 1000.0f);
            else if (property.name == "Static")
                m_isStatic = property.value == "true" || property.value == "1";
            else if (property.name == "HeightMap")
                heightMapPath = property.value;
            else if (property.name == "HeightData")
                heightDataPath = property.value;
            else if (property.name == "HeightSamples")
                serializedHeightSamples = property.Text();
            else if (property.name == "HoleMask")
                serializedHoles = property.Text();
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
            else if (property.name == "PaintLayer")
                SetPaintLayer(std::stoi(property.value));
            else if (property.name == "LayerOpacity")
                SetLayerOpacity(std::stof(property.value));
            else if (property.name == "BrushHardness")
                SetBrushHardness(std::stof(property.value));
            else if (property.name == "HeightBlend")
                heightBlend = std::stof(property.value);
            else if (property.name == "DetailDistance")
                detailDistance = std::stof(property.value);
            else if (property.name == "WeightResolution")
                weightResolution = glm::clamp(std::stoi(property.value), 16, 4096);
            else if (property.name.rfind("WeightMap", 0) == 0 && property.name.size() == 10)
            {
                const auto plane = static_cast<std::size_t>(property.name.back() - '0');
                if (plane < weightMapPaths.size())
                    weightMapPaths[plane] = property.value;
            }
            else if (property.name == "LayerCount")
            {
                layerCount = static_cast<std::size_t>(std::clamp(std::stoi(property.value), 0, int(TerrainLayerStack::kMaxLayers)));
                hasLayerCount = true;
            }
            else if (std::string field; auto *layer = layerAt(property.name, field))
            {
                if (field == "Name") layer->name = property.value;
                else if (field == "AlbedoMap") layer->albedoMap = property.value;
                else if (field == "NormalMap") layer->normalMap = property.value;
                else if (field == "RoughnessMap") layer->roughnessMap = property.value;
                else if (field == "HeightMap") layer->heightMap = property.value;
                else if (field == "Tint") layer->tint = ParseVec3(property.value, glm::vec3(1.0f));
                else if (field == "TileSize") layer->tileSize = std::max(0.01f, std::stof(property.value));
                else if (field == "NormalStrength") layer->normalStrength = std::max(0.0f, std::stof(property.value));
                else if (field == "Roughness") layer->roughness = std::max(0.0f, std::stof(property.value));
                else if (field == "Metallic") layer->metallic = glm::clamp(std::stof(property.value), 0.0f, 1.0f);
                else if (field == "AntiTiling") layer->antiTiling = property.value == "true" || property.value == "1";
            }
        }
        if (hasLayerCount)
            layers.resize(layerCount);
        if (layers != m_layers.GetLayers())
            m_layers.SetLayers(std::move(layers));
        m_layers.SetHeightBlend(heightBlend);
        m_layers.SetDetailDistance(detailDistance);
        // Reload the weights only when they come from different files: undoing an unrelated edit must not throw
        // away painting that is newer than the files.
        if (weightMapPaths != m_layers.GetWeightMapPaths())
        {
            m_layers.SetWeightMapPaths(weightMapPaths);
            if (!m_layers.LoadWeightMaps())
            {
                TerrainWeightMap weights(weightResolution);
                m_layers.SetWeights(std::move(weights));
            }
        }
        else if (weightResolution != m_layers.GetWeights().GetResolution())
            m_layers.SetWeightResolution(weightResolution);

        // Where the heights come from. A height file supersedes everything else, and is read only when the scene names
        // a different file than the one loaded: undoing an unrelated edit must not throw away sculpting newer than
        // the scene state (the rule the layer weight files follow). The file also owns the grid's size.
        const bool usesHeightFile = heightDataPath ? !heightDataPath->empty() : !m_heightDataPath.empty();
        if (usesHeightFile)
        {
            if (heightDataPath && *heightDataPath != m_heightDataPath)
            {
                m_heightDataPath = *heightDataPath;
                if (!LoadHeightData())
                    EnsureHeightStorage();
            }
            else
            {
                m_width = previousWidth;
                m_depth = previousDepth;
            }
            m_heightMapPath = heightMapPath;
            MarkMeshDirty();
        }
        else
        {
            if (!m_heightDataPath.empty())
            {
                // Back to heights held by the scene (undoing "store in file"): the samples follow, or the heights stay.
                m_heightDataPath.clear();
                m_border.clear();
                ++m_heightsRevision;
            }
            EnsureHeightStorage();
            const std::size_t expectedHeightSampleCount = static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_depth);
            if (!serializedHeightSamples.empty() &&
                DeserializeHeightSamples(serializedHeightSamples, expectedHeightSampleCount, m_heights))
            {
                ++m_heightsRevision;
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
        }

        if (serializedHoles)
        {
            if (auto holes = TerrainHoleMask::Decode(*serializedHoles, m_width - 1, m_depth - 1))
            {
                if (*holes != m_holes)
                {
                    m_holes = std::move(*holes);
                    HolesChanged();
                }
            }
        }

        m_materialAssetReference = materialAssetReference;
        RebuildMaterialFromReference();
        if (!paintedAlbedoPath.empty())
            SetPaintedAlbedoPath(paintedAlbedoPath);
    }

    bool TerrainComponent::ImportHeightMap(const std::string &filePath, const TerrainHeightImportOptions &options,
                                           std::optional<glm::vec2> heightRange, std::string *error)
    {
        std::vector<std::uint8_t> bytes;
        if (!ReadTerrainFile(ResolveTerrainFile(filePath), bytes))
        {
            if (error)
                *error = "Could not read " + filePath + ".";
            return false;
        }
        auto imported = DecodeTerrainHeightSource(bytes, filePath, options, error);
        if (!imported)
            return false;

        const glm::vec2 range = heightRange.value_or(glm::vec2(0.0f, m_heightScale));
        const auto toHeight = [&](float sample) {
            return glm::clamp(imported->normalized ? range.x + sample * (range.y - range.x) : sample, 0.0f, m_heightScale);
        };
        for (auto &sample : imported->field.samples)
            sample = toHeight(sample);
        for (auto &sample : imported->border)
            sample = toHeight(sample);
        const int width = imported->field.width;
        const int depth = imported->field.depth;
        ReplaceHeights(width, depth, std::move(imported->field.samples), std::move(imported->border));
        if (error)
            error->clear();
        return true;
    }

    bool TerrainComponent::LoadHeightMap(const std::string &filePath)
    {
        if (!ImportHeightMap(filePath))
            return false;
        m_heightMapPath = filePath;
        return true;
    }

    bool TerrainComponent::LoadHeightData(std::string *error)
    {
        if (m_heightDataPath.empty())
        {
            if (error)
                *error = "The terrain has no height file.";
            return false;
        }
        std::vector<std::uint8_t> bytes;
        if (!ReadTerrainFile(ResolveTerrainFile(m_heightDataPath), bytes))
        {
            if (error)
                *error = "Could not read " + m_heightDataPath + ".";
            return false;
        }
        auto data = TerrainHeightFile::Decode(bytes, error);
        if (!data)
            return false;
        SetHeightData(std::move(*data));
        return true;
    }

    bool TerrainComponent::SaveHeightData(TerrainHeightEncoding encoding, std::string *error) const
    {
        if (m_heightDataPath.empty())
        {
            if (error)
                *error = "The terrain has no height file.";
            return false;
        }
        const std::filesystem::path path = ResolveTerrainFile(m_heightDataPath);
        std::error_code ignored;
        if (path.has_parent_path())
            std::filesystem::create_directories(path.parent_path(), ignored);
        const auto bytes = TerrainHeightFile::Encode(GetHeightData(), encoding);
        // Written beside the target and moved over it, so a failed write never leaves half a terrain.
        const auto staging = path.string() + ".writing";
        {
            std::ofstream output(staging, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!output)
            {
                if (error)
                    *error = "Could not write " + path.string() + ".";
                return false;
            }
        }
        std::error_code renameError;
        std::filesystem::rename(staging, path, renameError);
        if (renameError)
        {
            std::filesystem::remove(staging, ignored);
            if (error)
                *error = "Could not replace " + path.string() + ": " + renameError.message();
            return false;
        }
        return true;
    }

    TerrainHeightData TerrainComponent::GetHeightData() const
    {
        TerrainHeightData data;
        data.field.width = m_width;
        data.field.depth = m_depth;
        data.field.samples = m_heights;
        data.border = m_border;
        return data;
    }

    void TerrainComponent::SetHeightData(TerrainHeightData data)
    {
        if (!data.Valid())
            return;
        for (auto &sample : data.field.samples)
            sample = glm::clamp(sample, 0.0f, m_heightScale);
        for (auto &sample : data.border)
            sample = glm::clamp(sample, 0.0f, m_heightScale);
        const int width = data.field.width;
        const int depth = data.field.depth;
        ReplaceHeights(width, depth, std::move(data.field.samples), std::move(data.border));
    }

    void TerrainComponent::ReplaceHeights(int width, int depth, std::vector<float> heights, std::vector<float> border)
    {
        if (width != m_width || depth != m_depth)
        {
            m_width = width;
            m_depth = depth;
            ResizeHoleMasks();
        }
        m_heights = std::move(heights);
        m_border = border.size() == TerrainHeightBorder::Size(m_width, m_depth) ? std::move(border) : std::vector<float>{};
        HeightsChanged();
        if (auto *owner = GetOwner())
            if (auto *scene = owner->GetScene())
                scene->MarkColliderShapesDirty();
    }

    void TerrainComponent::SetHeightBorder(std::vector<float> border)
    {
        if (border.size() != TerrainHeightBorder::Size(m_width, m_depth))
            border.clear();
        if (border == m_border)
            return;
        m_border = std::move(border);
        HeightsChanged();
    }

    void TerrainComponent::HeightsChanged()
    {
        ++m_heightsRevision;
        MarkMeshDirty();
    }

    TerrainHeightRegion TerrainComponent::CaptureHeights(const TerrainSampleRect &rect) const
    {
        TerrainHeightRegion region;
        region.rect = {std::max(rect.x0, 0), std::max(rect.z0, 0), std::min(rect.x1, m_width), std::min(rect.z1, m_depth)};
        if (region.rect.Empty())
            return region;
        region.samples.reserve(static_cast<std::size_t>(region.rect.x1 - region.rect.x0) * (region.rect.z1 - region.rect.z0));
        for (int z = region.rect.z0; z < region.rect.z1; ++z)
            for (int x = region.rect.x0; x < region.rect.x1; ++x)
                region.samples.push_back(m_heights[static_cast<std::size_t>(z) * m_width + x]);
        return region;
    }

    void TerrainComponent::RestoreHeights(const TerrainHeightRegion &region)
    {
        const int columns = region.rect.x1 - region.rect.x0;
        if (region.rect.Empty() || region.samples.size() != static_cast<std::size_t>(columns) * (region.rect.z1 - region.rect.z0))
            return;
        bool changed = false;
        for (int z = region.rect.z0; z < region.rect.z1; ++z)
            for (int x = region.rect.x0; x < region.rect.x1; ++x)
            {
                if (x < 0 || z < 0 || x >= m_width || z >= m_depth)
                    continue;
                const float value = glm::clamp(region.samples[static_cast<std::size_t>(z - region.rect.z0) * columns + (x - region.rect.x0)], 0.0f, m_heightScale);
                float &target = m_heights[static_cast<std::size_t>(z) * m_width + x];
                changed |= target != value;
                target = value;
            }
        if (changed)
        {
            HeightsChanged();
            if (auto *owner = GetOwner())
                if (auto *scene = owner->GetScene())
                    scene->MarkColliderShapesDirty();
        }
    }

    glm::ivec2 TerrainComponent::WorldToSample(const glm::vec3 &worldPosition) const
    {
        const auto *entity = GetOwner();
        const glm::vec3 local = entity ? glm::vec3(glm::inverse(entity->GetWorldTransform()) * glm::vec4(worldPosition, 1.0f)) : worldPosition;
        return {glm::clamp(static_cast<int>(std::lround(local.x / m_cellSize)), 0, m_width - 1),
                glm::clamp(static_cast<int>(std::lround(local.z / m_cellSize)), 0, m_depth - 1)};
    }

    void TerrainComponent::SetLodDistanceScale(float scale)
    {
        scale = glm::clamp(scale, 0.25f, 16.0f);
        if (scale == m_lodDistanceScale)
            return;
        m_lodDistanceScale = scale;
        MarkMeshDirty();
    }

    void TerrainComponent::SetSkirtDepth(float depth)
    {
        depth = glm::clamp(depth, 0.0f, 1000.0f);
        if (depth == m_skirtDepth)
            return;
        m_skirtDepth = depth;
        MarkMeshDirty();
    }

    bool TerrainComponent::IsRetiredProperty(std::string_view name) const
    {
        // The brush was saved with the terrain until the editor's terrain tool took it over.
        return name == "PaintEnabled" || name == "PaintMode" || name == "BrushRadius" || name == "BrushStrength" ||
               name == "FlattenHeight" || name == "PaintLayer" || name == "LayerOpacity" || name == "BrushHardness";
    }

    bool TerrainComponent::PaintAtWorldPosition(const glm::vec3 &worldPosition, float deltaTime)
    {
        if (!GetOwner() || !m_paintEnabled || m_paintMode == TerrainPaintMode::Layer)
            return false;
        if (m_paintMode == TerrainPaintMode::Hole || m_paintMode == TerrainPaintMode::Fill)
            return PaintHoles(worldPosition, TerrainHoleBrush{m_brushRadius, m_paintMode == TerrainPaintMode::Hole});
        return !SculptAtWorldPosition(worldPosition, deltaTime).Empty();
    }

    TerrainSampleRect TerrainComponent::SculptAtWorldPosition(const glm::vec3 &worldPosition, float deltaTime)
    {
        TerrainSculptBrush brush{TerrainSculptMode::Raise, m_brushRadius, m_brushStrength, m_flattenHeight};
        switch (m_paintMode)
        {
        case TerrainPaintMode::Raise: brush.mode = TerrainSculptMode::Raise; break;
        case TerrainPaintMode::Lower: brush.mode = TerrainSculptMode::Lower; break;
        case TerrainPaintMode::Smooth: brush.mode = TerrainSculptMode::Smooth; break;
        case TerrainPaintMode::Flatten: brush.mode = TerrainSculptMode::Flatten; break;
        default: return {};
        }
        return Sculpt(worldPosition, brush, deltaTime);
    }

    bool TerrainComponent::PaintHoles(const glm::vec3 &worldPosition, const TerrainHoleBrush &brush)
    {
        const auto *entity = GetOwner();
        if (!entity || !(brush.radius > 0.0f))
            return false;
        const glm::vec3 local = glm::vec3(glm::inverse(entity->GetWorldTransform()) * glm::vec4(worldPosition, 1.0f));
        if (!m_holes.PaintCircle(local.x / m_cellSize, local.z / m_cellSize, brush.radius / m_cellSize, brush.cut))
            return false;
        HolesChanged();
        return true;
    }

    TerrainWeightRect TerrainComponent::PaintLayer(const glm::vec3 &worldPosition, const TerrainPaintBrush &brush)
    {
        return PaintLayerAtWorldPosition(worldPosition, brush.layer, brush.radius, brush.dab);
    }

    TerrainSampleRect TerrainComponent::Sculpt(const glm::vec3 &worldPosition, const TerrainSculptBrush &brush, float deltaTime)
    {
        auto *entity = GetOwner();
        if (!entity || !(brush.radius > 0.0f))
        {
            return {};
        }
        const float brushRadius = brush.radius;
        const float brushStrength = std::max(brush.strength, 0.0f);

        const glm::mat4 inverseWorld = glm::inverse(entity->GetWorldTransform());
        const glm::vec3 local = glm::vec3(inverseWorld * glm::vec4(worldPosition, 1.0f));
        const float centerX = local.x / m_cellSize;
        const float centerZ = local.z / m_cellSize;
        const float radiusInSamples = brushRadius / m_cellSize;
        const int minX = std::max(0, static_cast<int>(std::floor(centerX - radiusInSamples)));
        const int maxX = std::min(m_width - 1, static_cast<int>(std::ceil(centerX + radiusInSamples)));
        const int minZ = std::max(0, static_cast<int>(std::floor(centerZ - radiusInSamples)));
        const int maxZ = std::min(m_depth - 1, static_cast<int>(std::ceil(centerZ + radiusInSamples)));
        if (minX > maxX || minZ > maxZ)
        {
            return {};
        }

        // Smoothing reads the samples around each one as they were before this dab: keep a copy of the brushed
        // samples and a one-sample margin, not of the whole grid.
        const TerrainHeightRegion source = CaptureHeights({minX - 1, minZ - 1, maxX + 2, maxZ + 2});
        const auto sourceHeight = [&](int x, int z) {
            // Beyond the grid: the border ring (a neighbouring tile's samples), so smoothing is seamless across tiles.
            if (x < 0 || z < 0 || x >= m_width || z >= m_depth)
                return GetHeightSample(x, z);
            return source.samples[static_cast<std::size_t>(z - source.rect.z0) * (source.rect.x1 - source.rect.x0) + (x - source.rect.x0)];
        };
        const float signedStrength = (brush.mode == TerrainSculptMode::Lower ? -1.0f : 1.0f) * brushStrength * std::max(deltaTime, 1.0f / 120.0f);
        TerrainSampleRect changed;
        for (int z = minZ; z <= maxZ; ++z)
        {
            for (int x = minX; x <= maxX; ++x)
            {
                const float dx = (static_cast<float>(x) - centerX) * m_cellSize;
                const float dz = (static_cast<float>(z) - centerZ) * m_cellSize;
                const float distance = std::sqrt(dx * dx + dz * dz);
                if (distance > brushRadius)
                {
                    continue;
                }

                const float falloff = 1.0f - glm::smoothstep(0.0f, brushRadius, distance);
                const std::size_t index = static_cast<std::size_t>(z * m_width + x);
                const float current = sourceHeight(x, z);
                float nextHeight = current;
                if (brush.mode == TerrainSculptMode::Smooth)
                {
                    float total = 0.0f;
                    for (int oz = -1; oz <= 1; ++oz)
                        for (int ox = -1; ox <= 1; ++ox)
                            total += sourceHeight(x + ox, z + oz);
                    nextHeight = glm::mix(current, total / 9.0f, glm::clamp(falloff * brushStrength * deltaTime, 0.0f, 1.0f));
                }
                else if (brush.mode == TerrainSculptMode::Flatten)
                {
                    nextHeight = glm::mix(current, brush.flattenHeight, glm::clamp(falloff * brushStrength * deltaTime, 0.0f, 1.0f));
                }
                else
                {
                    nextHeight = current + signedStrength * falloff;
                }

                nextHeight = glm::clamp(nextHeight, 0.0f, m_heightScale);
                if (std::abs(nextHeight - m_heights[index]) > 0.00001f)
                {
                    m_heights[index] = nextHeight;
                    changed.Include({x, z, x + 1, z + 1});
                }
            }
        }

        if (!changed.Empty())
        {
            HeightsChanged();
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

        const glm::vec3 boundsMin(0.0f);
        const glm::vec3 boundsMax(static_cast<float>(m_width - 1) * m_cellSize,
                                  m_heightScale,
                                  static_cast<float>(m_depth - 1) * m_cellSize);
        float entryDistance = 0.0f;
        float exitDistance = std::numeric_limits<float>::max();
        for (int axis = 0; axis < 3; ++axis)
        {
            if (std::abs(localDirection[axis]) <= kRayEpsilon)
            {
                if (localOrigin[axis] < boundsMin[axis] || localOrigin[axis] > boundsMax[axis])
                    return false;
                continue;
            }
            float nearDistance = (boundsMin[axis] - localOrigin[axis]) / localDirection[axis];
            float farDistance = (boundsMax[axis] - localOrigin[axis]) / localDirection[axis];
            if (nearDistance > farDistance)
                std::swap(nearDistance, farDistance);
            entryDistance = std::max(entryDistance, nearDistance);
            exitDistance = std::min(exitDistance, farDistance);
            if (entryDistance > exitDistance)
                return false;
        }

        const auto heightDelta = [&](float distance)
        {
            const glm::vec3 point = localOrigin + localDirection * distance;
            return point.y - SampleHeight(point.x / m_cellSize, point.z / m_cellSize);
        };

        // Sampling at most half a heightfield cell apart avoids the O(width*depth)
        // triangle walk formerly paid every frame while the paint cursor was visible.
        const float horizontalSpeed = glm::length(glm::vec2(localDirection.x, localDirection.z));
        const float stepDistance = horizontalSpeed > kRayEpsilon
                                       ? (0.5f * m_cellSize) / horizontalSpeed
                                       : std::max(0.5f * m_cellSize, 0.01f);
        float previousDistance = entryDistance;
        float previousDelta = heightDelta(previousDistance);
        float selectedDistance = std::numeric_limits<float>::max();
        if (std::abs(previousDelta) <= kRayEpsilon &&
            !GetEffectiveHoles().IsHoleAt((localOrigin.x + localDirection.x * previousDistance) / m_cellSize,
                              (localOrigin.z + localDirection.z * previousDistance) / m_cellSize))
            selectedDistance = previousDistance;

        for (float distance = std::min(entryDistance + stepDistance, exitDistance);
             selectedDistance == std::numeric_limits<float>::max() && distance <= exitDistance;
             distance = std::min(distance + stepDistance, exitDistance))
        {
            const float delta = heightDelta(distance);
            if ((previousDelta <= 0.0f && delta >= 0.0f) || (previousDelta >= 0.0f && delta <= 0.0f))
            {
                float low = previousDistance;
                float high = distance;
                for (int iteration = 0; iteration < 12; ++iteration)
                {
                    const float middle = (low + high) * 0.5f;
                    const float middleDelta = heightDelta(middle);
                    if ((previousDelta <= 0.0f && middleDelta <= 0.0f) ||
                        (previousDelta >= 0.0f && middleDelta >= 0.0f))
                        low = middle;
                    else
                        high = middle;
                }
                const float crossing = (low + high) * 0.5f;
                const glm::vec3 point = localOrigin + localDirection * crossing;
                // A crossing inside a hole is not a surface: the ray passes through and may meet the terrain beyond.
                if (!GetEffectiveHoles().IsHoleAt(point.x / m_cellSize, point.z / m_cellSize))
                {
                    selectedDistance = crossing;
                    break;
                }
            }
            if (distance >= exitDistance)
                break;
            previousDistance = distance;
            previousDelta = delta;
        }

        if (selectedDistance == std::numeric_limits<float>::max())
            return false;

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
        m_border.clear();
        ++m_heightsRevision;
        if (m_holes.GetCellsX() != m_width - 1 || m_holes.GetCellsZ() != m_depth - 1)
        {
            ResizeHoleMasks();
        }
        MarkMeshDirty();
    }

    float TerrainComponent::GetHeightAtLocalPosition(float x, float z) const
    {
        return SampleHeight(x / m_cellSize, z / m_cellSize);
    }

    bool TerrainComponent::TrySampleSurface(float x, float z, float &height, glm::vec3 &normal) const
    {
        const float maxX = (m_width - 1) * m_cellSize;
        const float maxZ = (m_depth - 1) * m_cellSize;
        if (!std::isfinite(x) || !std::isfinite(z) || !std::isfinite(m_cellSize) || m_cellSize <= 0 ||
            x < 0 || z < 0 || x > maxX || z > maxZ)
            return false;
        if (GetEffectiveHoles().IsHoleAt(x / m_cellSize, z / m_cellSize))
            return false;

        height = GetHeightAtLocalPosition(x, z);
        // One-sided differences at the boundary avoid flattening the normal
        // by sampling a clamped height outside the heightfield.
        const float left = std::max(0.0f, x - m_cellSize * 0.5f);
        const float right = std::min(maxX, x + m_cellSize * 0.5f);
        const float down = std::max(0.0f, z - m_cellSize * 0.5f);
        const float up = std::min(maxZ, z + m_cellSize * 0.5f);
        if (right <= left || up <= down) return false;
        const float dx = (GetHeightAtLocalPosition(right, z) - GetHeightAtLocalPosition(left, z)) / (right - left);
        const float dz = (GetHeightAtLocalPosition(x, up) - GetHeightAtLocalPosition(x, down)) / (up - down);
        normal = glm::normalize(glm::vec3(-dx, 1.0f, -dz));
        return std::isfinite(height) && std::isfinite(normal.x) && std::isfinite(normal.y) && std::isfinite(normal.z);
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

    TerrainLayerStack::Services TerrainComponent::EngineLayerServices()
    {
        TerrainLayerStack::Services services;
        services.loadTexture = [](const std::string &reference, render::TextureColorSpace colorSpace) -> const render::Texture * {
            return core::Engine::GetInstance().GetAssetManager().LoadTexture(reference.c_str(), colorSpace);
        };
        services.resolvePath = [](const std::string &reference) {
            auto resolved = core::Engine::GetInstance().GetAssetManager().ResolveAssetPath(reference);
            return resolved.empty() ? reference : resolved;
        };
        services.readFile = [](const std::string &path, std::string &bytes) {
            if (content::IsMounted(path))
                return content::ReadFile(path, bytes);
            std::ifstream input(path, std::ios::binary);
            if (!input)
                return false;
            bytes.assign(std::istreambuf_iterator<char>(input), {});
            return true;
        };
        return services;
    }

    render::Material *TerrainComponent::GetRenderMaterial()
    {
        if (m_layers.Empty() || !m_material)
            return m_material;
        // A private copy of the terrain's material carries the layers; it follows edits to that material.
        const auto baseRevision = m_material->GetRevision();
        if (!m_layeredMaterial || m_layeredBase != m_material || (baseRevision != 0 && baseRevision != m_layeredBaseRevision))
        {
            auto config = m_material->ReadConfig();
            // The terrain variant is a standard material: graph and material maps give way to the layers.
            config.shaderGraphProgram = nullptr;
            config.normalTexture = config.metallicTexture = config.roughnessTexture = nullptr;
            config.terrainLayers = nullptr;
            if (!m_layeredMaterial)
                m_layeredMaterial = std::make_unique<render::Material>(config);
            else
                m_layeredMaterial->SetConfig(std::move(config));
            m_layeredBase = m_material;
            m_layeredBaseRevision = baseRevision;
        }
        m_layeredMaterial->SetTerrainLayers(m_layers.GetRenderSet());
        m_layeredMaterial->SetAlbedoTexture(const_cast<render::Texture *>(m_layers.GetMacroAlbedo()));
        return m_layeredMaterial.get();
    }

    glm::vec2 TerrainComponent::WorldToTerrainUv(const glm::vec3 &worldPosition) const
    {
        const auto *entity = GetOwner();
        const glm::vec3 local = entity ? glm::vec3(glm::inverse(entity->GetWorldTransform()) * glm::vec4(worldPosition, 1.0f)) : worldPosition;
        const glm::vec2 extent(std::max(1, m_width - 1) * m_cellSize, std::max(1, m_depth - 1) * m_cellSize);
        return glm::vec2(local.x, local.z) / extent;
    }

    TerrainWeightRect TerrainComponent::PaintLayerAtWorldPosition(const glm::vec3 &worldPosition)
    {
        return PaintLayerAtWorldPosition(worldPosition, static_cast<std::size_t>(m_paintLayer), m_brushRadius,
                                         TerrainLayerBrush{m_layerOpacity, m_brushHardness});
    }

    TerrainWeightRect TerrainComponent::PaintLayerAtWorldPosition(const glm::vec3 &worldPosition, std::size_t layer, float radius,
                                                                  const TerrainLayerBrush &brush)
    {
        if (!GetOwner() || layer >= m_layers.GetLayers().size() || !(radius > 0.0f))
            return {};
        // The radius is in the terrain's local units, like the sculpting brush.
        const glm::vec2 extent(std::max(1, m_width - 1) * m_cellSize, std::max(1, m_depth - 1) * m_cellSize);
        return m_layers.Paint(WorldToTerrainUv(worldPosition), glm::vec2(radius) / extent, layer, brush);
    }

    bool TerrainComponent::TrySampleWorldSurface(const glm::vec3 &worldPosition, float &worldHeight, glm::vec3 &worldNormal) const
    {
        const auto *entity = GetOwner();
        if (!entity)
            return false;
        const glm::mat4 world = entity->GetWorldTransform();
        const glm::vec3 local = glm::vec3(glm::inverse(world) * glm::vec4(worldPosition, 1.0f));
        float height = 0.0f;
        glm::vec3 normal(0.0f, 1.0f, 0.0f);
        if (!TrySampleSurface(local.x, local.z, height, normal))
            return false;
        worldHeight = glm::vec3(world * glm::vec4(local.x, height, local.z, 1.0f)).y;
        worldNormal = glm::normalize(glm::transpose(glm::inverse(glm::mat3(world))) * normal);
        return true;
    }

    void TerrainComponent::SetPaintedAlbedoPath(const std::string &path)
    {
        if (path.empty())
            return;
        const auto resolvedPath = core::Engine::GetInstance().GetAssetManager().ResolveAssetPath(path);
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
        if (m_heights.empty())
            return 0.0f;
        if (!m_border.empty())
        {
            // Outside the grid, the border ring holds what the neighbouring tiles have there.
            x = glm::clamp(x, -TerrainHeightBorder::Width, m_width - 1 + TerrainHeightBorder::Width);
            z = glm::clamp(z, -TerrainHeightBorder::Width, m_depth - 1 + TerrainHeightBorder::Width);
            if (x < 0 || z < 0 || x >= m_width || z >= m_depth)
                return m_border[TerrainHeightBorder::Index(m_width, m_depth, x, z)];
        }
        x = glm::clamp(x, 0, m_width - 1);
        z = glm::clamp(z, 0, m_depth - 1);
        return m_heights[static_cast<std::size_t>(z * m_width + x)];
    }

    void TerrainComponent::SetHeightSample(int x, int z, float height)
    {
        if (x < 0 || x >= m_width || z < 0 || z >= m_depth || m_heights.empty())
        {
            return;
        }
        m_heights[static_cast<std::size_t>(z * m_width + x)] = glm::clamp(height, 0.0f, m_heightScale);
        ++m_heightsRevision;
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

    float TerrainComponent::GetSurfaceSampleHeight(int x, int z) const
    {
        const float raw = GetHeightSample(x, z);
        if (m_surfaceSmoothing <= 0.0f)
        {
            return raw;
        }

        float total = 0.0f;
        float weight = 0.0f;
        for (int offsetZ = -1; offsetZ <= 1; ++offsetZ)
        {
            for (int offsetX = -1; offsetX <= 1; ++offsetX)
            {
                const float sampleWeight = (offsetX == 0 && offsetZ == 0) ? 4.0f :
                                           (offsetX == 0 || offsetZ == 0 ? 2.0f : 1.0f);
                total += GetHeightSample(x + offsetX, z + offsetZ) * sampleWeight;
                weight += sampleWeight;
            }
        }
        return glm::mix(raw, total / std::max(weight, 0.0001f), m_surfaceSmoothing);
    }

    void TerrainComponent::EnsureHeightStorage()
    {
        if (m_holes.GetCellsX() != m_width - 1 || m_holes.GetCellsZ() != m_depth - 1)
        {
            ResizeHoleMasks();
        }

        const std::size_t targetSize = static_cast<std::size_t>(m_width * m_depth);
        if (m_heights.size() == targetSize)
        {
            return;
        }

        m_heights.assign(targetSize, 0.0f);
        ++m_heightsRevision;
    }

    void TerrainComponent::SetHoles(TerrainHoleMask holes)
    {
        holes.Resize(m_width - 1, m_depth - 1);
        if (holes == m_holes)
            return;
        m_holes = std::move(holes);
        HolesChanged();
    }

    bool TerrainComponent::SetHole(int cellX, int cellZ, bool hole)
    {
        if (!m_holes.Set(cellX, cellZ, hole))
            return false;
        HolesChanged();
        return true;
    }

    bool TerrainComponent::SmoothHoleEdges()
    {
        if (!m_holes.SmoothEdges())
            return false;
        HolesChanged();
        return true;
    }

    bool TerrainComponent::IsHoleAtLocalPosition(float x, float z) const
    {
        return GetEffectiveHoles().IsHoleAt(x / m_cellSize, z / m_cellSize);
    }

    bool TerrainComponent::IsHoleAtWorldPosition(const glm::vec3 &worldPosition) const
    {
        const auto *entity = GetOwner();
        if (!entity || GetEffectiveHoles().Empty())
            return false;
        const glm::vec3 local = glm::vec3(glm::inverse(entity->GetWorldTransform()) * glm::vec4(worldPosition, 1.0f));
        return IsHoleAtLocalPosition(local.x, local.z);
    }

    void TerrainComponent::SetDerivedHoles(std::string_view source, TerrainHoleMask holes)
    {
        const auto found = m_derivedHoles.find(source);
        if (holes.Empty())
        {
            if (found == m_derivedHoles.end())
                return;
            m_derivedHoles.erase(found);
        }
        else
        {
            holes.Resize(m_width - 1, m_depth - 1);
            if (found != m_derivedHoles.end() && found->second == holes)
                return;
            m_derivedHoles.insert_or_assign(std::string(source), std::move(holes));
        }
        HolesChanged();
    }

    const TerrainHoleMask *TerrainComponent::FindDerivedHoles(std::string_view source) const
    {
        const auto found = m_derivedHoles.find(source);
        return found == m_derivedHoles.end() ? nullptr : &found->second;
    }

    void TerrainComponent::ResizeHoleMasks()
    {
        m_holes.Resize(m_width - 1, m_depth - 1);
        for (auto &[source, mask] : m_derivedHoles)
            mask.Resize(m_width - 1, m_depth - 1);
        RebuildEffectiveHoles();
        ++m_holesRevision;
    }

    void TerrainComponent::RebuildEffectiveHoles()
    {
        if (m_derivedHoles.empty())
        {
            m_effectiveHoles = TerrainHoleMask{};
            return;
        }
        m_effectiveHoles = m_holes;
        for (const auto &[source, mask] : m_derivedHoles)
            m_effectiveHoles.Unite(mask);
    }

    void TerrainComponent::HolesChanged()
    {
        RebuildEffectiveHoles();
        ++m_holesRevision;
        MarkMeshDirty();
        if (auto *owner = GetOwner())
            if (auto *scene = owner->GetScene())
                scene->MarkColliderShapesDirty();
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

    // What a terrain's mesh is built from, copied so the build can run on a worker thread (world tiles).
    struct TerrainMeshInputs
    {
        int width = 0;
        int depth = 0;
        float cellSize = 1.0f;
        float surfaceSmoothing = 0.0f;
        float skirtDepth = 0.0f;
        float lodDistanceScale = 1.0f;
        int chunkSize = 32;
        int lodCount = 1;
        std::vector<float> heights;
        std::vector<float> border;
        TerrainHoleMask holes;

        // As TerrainComponent::GetHeightSample.
        float HeightSample(int x, int z) const
        {
            if (heights.empty())
                return 0.0f;
            if (!border.empty())
            {
                x = glm::clamp(x, -TerrainHeightBorder::Width, width - 1 + TerrainHeightBorder::Width);
                z = glm::clamp(z, -TerrainHeightBorder::Width, depth - 1 + TerrainHeightBorder::Width);
                if (x < 0 || z < 0 || x >= width || z >= depth)
                    return border[TerrainHeightBorder::Index(width, depth, x, z)];
            }
            x = glm::clamp(x, 0, width - 1);
            z = glm::clamp(z, 0, depth - 1);
            return heights[static_cast<std::size_t>(z * width + x)];
        }

        // As TerrainComponent::GetSurfaceSampleHeight.
        float SurfaceHeight(int x, int z) const
        {
            const float raw = HeightSample(x, z);
            if (surfaceSmoothing <= 0.0f)
                return raw;
            float total = 0.0f;
            float weight = 0.0f;
            for (int offsetZ = -1; offsetZ <= 1; ++offsetZ)
                for (int offsetX = -1; offsetX <= 1; ++offsetX)
                {
                    const float sampleWeight = (offsetX == 0 && offsetZ == 0) ? 4.0f : (offsetX == 0 || offsetZ == 0 ? 2.0f : 1.0f);
                    total += HeightSample(x + offsetX, z + offsetZ) * sampleWeight;
                    weight += sampleWeight;
                }
            return glm::mix(raw, total / std::max(weight, 0.0001f), surfaceSmoothing);
        }
    };

    namespace
    {
        render::MeshConfig BuildTerrainMesh(const TerrainMeshInputs &in)
        {
            const int surfaceStride = in.width + 2;
            std::vector<float> surface(static_cast<std::size_t>(surfaceStride) * static_cast<std::size_t>(in.depth + 2));
            for (int z = -1; z <= in.depth; ++z)
                for (int x = -1; x <= in.width; ++x)
                    surface[static_cast<std::size_t>(z + 1) * surfaceStride + static_cast<std::size_t>(x + 1)] = in.SurfaceHeight(x, z);
            const auto smoothedHeight = [&](int x, int z) {
                return surface[static_cast<std::size_t>(z + 1) * surfaceStride + static_cast<std::size_t>(x + 1)];
            };

            const auto coarseHeight = [&](int x, int z, int step)
            {
                const int x0 = (x / step) * step;
                const int z0 = (z / step) * step;
                const int x1 = std::min(x0 + step, in.width - 1);
                const int z1 = std::min(z0 + step, in.depth - 1);
                if (x1 == x0 || z1 == z0)
                {
                    return smoothedHeight(x, z);
                }

                const float tx = static_cast<float>(x - x0) / static_cast<float>(x1 - x0);
                const float tz = static_cast<float>(z - z0) / static_cast<float>(z1 - z0);
                const float h00 = smoothedHeight(x0, z0);
                const float h10 = smoothedHeight(x1, z0);
                const float h01 = smoothedHeight(x0, z1);
                const float h11 = smoothedHeight(x1, z1);

                // Match appendQuad's diagonal so the fully morphed fine mesh lies
                // exactly on the next LOD's piecewise-linear surface.
                if (tx + tz <= 1.0f)
                {
                    return h00 + (h10 - h00) * tx + (h01 - h00) * tz;
                }
                return h11 + (h01 - h11) * (1.0f - tx) + (h10 - h11) * (1.0f - tz);
            };

            render::MeshConfig config;
            config.data.vertices.reserve(static_cast<std::size_t>(in.width * in.depth));
            for (int z = 0; z < in.depth; ++z)
            {
                for (int x = 0; x < in.width; ++x)
                {
                    // Central differences of the smoothed surface.
                    const glm::vec3 normal = glm::normalize(glm::vec3(smoothedHeight(x - 1, z) - smoothedHeight(x + 1, z), 2.0f * in.cellSize,
                                                                      smoothedHeight(x, z - 1) - smoothedHeight(x, z + 1)));
                    render::MeshVertexData vertex;
                    const float height = smoothedHeight(x, z);
                    for (int morphIndex = 0; morphIndex < 4; ++morphIndex)
                    {
                        vertex.weights[static_cast<std::size_t>(morphIndex)] = coarseHeight(x, z, 2 << morphIndex);
                    }
                    vertex.joints[0] = static_cast<int>(std::round(coarseHeight(x, z, 32) * 4096.0f));
                    vertex.position = {static_cast<float>(x) * in.cellSize, height, static_cast<float>(z) * in.cellSize};
                    vertex.normal = {normal.x, normal.y, normal.z};
                    vertex.uv = {static_cast<float>(x) / static_cast<float>(std::max(1, in.width - 1)),
                                 static_cast<float>(z) / static_cast<float>(std::max(1, in.depth - 1))};
                    vertex.tangent = {1.0f, 0.0f, 0.0f, 1.0f};
                    config.data.vertices.push_back(vertex);
                }
            }

            const auto appendQuad = [&](int x, int z, int step, const auto &self) -> void
            {
                const int x1 = std::min(x + step, in.width - 1);
                const int z1 = std::min(z + step, in.depth - 1);
                if (x1 == x || z1 == z)
                {
                    return;
                }
                // Holes stay exact at every LOD: a coarse quad over a hole is replaced by the full-detail quads of its solid
                // cells. Their vertices geomorph onto the coarse surface like the rest of the LOD, so no cracks open.
                const bool cut = in.holes.AnyInRect(x, z, x1, z1);
                if (cut && step > 1)
                {
                    for (int fineZ = z; fineZ < z1; ++fineZ)
                        for (int fineX = x; fineX < x1; ++fineX)
                            self(fineX, fineZ, 1, self);
                    return;
                }

                const unsigned int i0 = static_cast<unsigned int>(z * in.width + x);
                const unsigned int i1 = static_cast<unsigned int>(z * in.width + x1);
                const unsigned int i2 = static_cast<unsigned int>(z1 * in.width + x);
                const unsigned int i3 = static_cast<unsigned int>(z1 * in.width + x1);
                for (const auto &triangle : TerrainCellTriangles(cut ? in.holes.GetCut(x, z) : TerrainHoleCut::None))
                {
                    const unsigned int corners[4] = {i0, i1, i2, i3};
                    config.data.indices.push_back(corners[triangle[0]]);
                    config.data.indices.push_back(corners[triangle[1]]);
                    config.data.indices.push_back(corners[triangle[2]]);
                }
            };

            // Skirts: a copy of every outer edge sample, lowered, so a wall hangs from the terrain's edge and hides cracks
            // against neighbouring terrain drawn at another detail (tiles, distant overview tiles).
            const bool skirts = in.skirtDepth > 0.0f;
            std::array<std::uint32_t, 4> skirtStart{}; // first skirt vertex of the edges z = 0, z = depth - 1, x = 0, x = width - 1
            if (skirts)
            {
                const auto lowered = [&](int x, int z) {
                    auto vertex = config.data.vertices[static_cast<std::size_t>(z) * in.width + x];
                    vertex.position[1] -= in.skirtDepth;
                    for (auto &weight : vertex.weights)
                        weight -= in.skirtDepth;
                    vertex.joints[0] -= static_cast<int>(std::round(in.skirtDepth * 4096.0f));
                    return vertex;
                };
                for (int edge = 0; edge < 4; ++edge)
                {
                    skirtStart[edge] = static_cast<std::uint32_t>(config.data.vertices.size());
                    const int count = edge < 2 ? in.width : in.depth;
                    for (int i = 0; i < count; ++i)
                        config.data.vertices.push_back(edge == 0 ? lowered(i, 0) : edge == 1 ? lowered(i, in.depth - 1)
                                                     : edge == 2 ? lowered(0, i) : lowered(in.width - 1, i));
                }
            }
            // The skirt along the samples [from, to] of an edge, every `step` samples. Both windings, so it shows from
            // either side whatever the material's culling.
            const auto appendSkirt = [&](int edge, int from, int to, int step) {
                for (int i = from; i < to; i += step)
                {
                    const int j = std::min(i + step, to);
                    if (j == i)
                        continue;
                    const auto grid = [&](int k) {
                        return static_cast<unsigned int>(edge == 0 ? k : edge == 1 ? (in.depth - 1) * in.width + k
                                                       : edge == 2 ? k * in.width : k * in.width + (in.width - 1));
                    };
                    const unsigned int a = grid(i), b = grid(j);
                    const unsigned int lowA = skirtStart[edge] + static_cast<unsigned int>(i), lowB = skirtStart[edge] + static_cast<unsigned int>(j);
                    for (const unsigned int index : {a, b, lowB, a, lowB, lowA, a, lowB, b, a, lowA, lowB})
                        config.data.indices.push_back(index);
                }
            };

            const int chunkQuads = std::max(2, in.chunkSize);
            for (int chunkZ = 0; chunkZ < in.depth - 1; chunkZ += chunkQuads)
            {
                for (int chunkX = 0; chunkX < in.width - 1; chunkX += chunkQuads)
                {
                    render::Submesh submesh;
                    submesh.materialIndex = 0;
                    submesh.name = "Terrain Chunk " + std::to_string(chunkX / chunkQuads) + "," + std::to_string(chunkZ / chunkQuads);

                    for (int lodIndex = 0; lodIndex < in.lodCount; ++lodIndex)
                    {
                        const int step = 1 << lodIndex;
                        const uint32_t lodOffset = static_cast<uint32_t>(config.data.indices.size());
                        const int maxZ = std::min(chunkZ + chunkQuads, in.depth - 1);
                        const int maxX = std::min(chunkX + chunkQuads, in.width - 1);
                        for (int z = chunkZ; z < maxZ; z += step)
                        {
                            for (int x = chunkX; x < maxX; x += step)
                            {
                                appendQuad(x, z, step, appendQuad);
                            }
                        }
                        if (skirts)
                        {
                            if (chunkZ == 0)
                                appendSkirt(0, chunkX, maxX, step);
                            if (maxZ == in.depth - 1)
                                appendSkirt(1, chunkX, maxX, step);
                            if (chunkX == 0)
                                appendSkirt(2, chunkZ, maxZ, step);
                            if (maxX == in.width - 1)
                                appendSkirt(3, chunkZ, maxZ, step);
                        }

                        const uint32_t lodCount = static_cast<uint32_t>(config.data.indices.size()) - lodOffset;
                        submesh.lods.push_back(render::Submesh::LodRange{
                            .indexOffset = lodOffset,
                            .indexCount = lodCount,
                            .minDistanceFactor = static_cast<float>(lodIndex) * 2.0f,
                            .maxScreenRadiusPixels = lodIndex == 0 ? std::numeric_limits<float>::max()
                                                                   : 280.0f / (in.lodDistanceScale * static_cast<float>(1 << (lodIndex - 1))),
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

            return config;
        }
    }

    void TerrainComponent::RebuildMesh()
    {
        if (m_meshBuild.valid())
            m_meshBuild.wait();
        PollMeshBuild();
        core::CpuScope scope("Terrain.RebuildMesh");
        EnsureHeightStorage();
        m_mesh.reset(render::Mesh::FromConfig(BuildTerrainMesh(CaptureMeshInputs())));
        m_meshDirty = false;
        m_hasPreviousModelMatrix = false;
    }

    void TerrainComponent::FinishMeshBuild()
    {
        if (m_meshDirty || m_meshBuild.valid())
            RebuildMesh();
    }

    TerrainMeshInputs TerrainComponent::CaptureMeshInputs() const
    {
        return TerrainMeshInputs{.width = m_width, .depth = m_depth, .cellSize = m_cellSize, .surfaceSmoothing = m_surfaceSmoothing,
                                 .skirtDepth = m_skirtDepth, .lodDistanceScale = m_lodDistanceScale, .chunkSize = m_chunkSize,
                                 .lodCount = m_lodCount, .heights = m_heights, .border = m_border, .holes = GetEffectiveHoles()};
    }

    void TerrainComponent::StartMeshBuild()
    {
        // One build at a time: a change made while one runs starts the next when it lands.
        if (m_meshBuild.valid())
            return;
        EnsureHeightStorage();
        m_meshDirty = false;
        m_meshBuild = std::async(std::launch::async, [inputs = CaptureMeshInputs()] { return BuildTerrainMesh(inputs); });
    }

    void TerrainComponent::PollMeshBuild()
    {
        if (!m_meshBuild.valid() || m_meshBuild.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        m_mesh.reset(render::Mesh::FromConfig(m_meshBuild.get()));
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
