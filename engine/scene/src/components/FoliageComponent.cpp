#include "PlutoGE/scene/components/FoliageComponent.h"

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <unordered_map>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

namespace PlutoGE::scene
{
    namespace
    {
        constexpr const char *kTypePrefix = "Type.";

        std::string SerializeInstances(const std::vector<FoliageInstance> &instances)
        {
            std::ostringstream output;
            for (std::size_t index = 0; index < instances.size(); ++index)
            {
                const auto &instance = instances[index];
                if (index > 0)
                {
                    output << ';';
                }
                output << instance.id << ','
                       << instance.position.x << ',' << instance.position.y << ',' << instance.position.z << ','
                       << instance.rotationDegrees.x << ',' << instance.rotationDegrees.y << ',' << instance.rotationDegrees.z << ','
                       << instance.scale.x << ',' << instance.scale.y << ',' << instance.scale.z;
            }
            return output.str();
        }

        std::vector<FoliageInstance> DeserializeInstances(const std::string &value)
        {
            std::vector<FoliageInstance> instances;
            std::size_t begin = 0;
            while (begin < value.size())
            {
                const std::size_t end = value.find(';', begin);
                const std::string record = value.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
                std::stringstream stream(record);
                std::string token;
                float values[9]{};
                std::uint64_t instanceId = 0;
                const auto commaCount = static_cast<int>(std::count(record.begin(), record.end(), ','));
                if (commaCount == 9 && std::getline(stream, token, ','))
                {
                    try { instanceId = std::stoull(token); } catch (...) { instanceId = 0; }
                }
                int valueIndex = 0;
                while (valueIndex < 9 && std::getline(stream, token, ','))
                {
                    try
                    {
                        values[valueIndex++] = std::stof(token);
                    }
                    catch (...)
                    {
                        valueIndex = 0;
                        break;
                    }
                }
                if (valueIndex == 9)
                {
                    instances.push_back(FoliageInstance{
                        .id = instanceId,
                        .position = {values[0], values[1], values[2]},
                        .rotationDegrees = {values[3], values[4], values[5]},
                        .scale = {values[6], values[7], values[8]},
                    });
                }
                if (end == std::string::npos)
                {
                    break;
                }
                begin = end + 1;
            }
            return instances;
        }

        std::string SerializeSubmeshIndices(const std::vector<int> &indices)
        {
            std::ostringstream output;
            for (std::size_t index = 0; index < indices.size(); ++index)
            {
                if (index > 0)
                {
                    output << ',';
                }
                output << indices[index];
            }
            return output.str();
        }

        std::vector<int> DeserializeSubmeshIndices(const std::string &value)
        {
            std::vector<int> indices;
            std::stringstream stream(value);
            std::string token;
            while (std::getline(stream, token, ','))
            {
                try
                {
                    indices.push_back(std::stoi(token));
                }
                catch (...)
                {
                }
            }
            return indices;
        }

        glm::vec3 SafeNormalizedAxis(const glm::vec3 &axis, const glm::vec3 &fallback)
        {
            const float lengthSquared = glm::length2(axis);
            return lengthSquared > 1e-8f ? axis / std::sqrt(lengthSquared) : fallback;
        }

        glm::mat4 ComposeInstanceTransform(const glm::mat4 &ownerTransform,
                                           const FoliageInstance &instance,
                                           const glm::vec3 &localOriginOffset = glm::vec3(0.0f),
                                           bool applyInstanceScale = true)
        {
            // Foliage positions are stored in the owner's local space, but instance
            // scale is intended to describe the foliage mesh itself. Inheriting a
            // scaled terrain transform here makes a scale of 1 produce enormous
            // foliage (and distorted collision shapes).
            const glm::vec3 worldPosition = glm::vec3(ownerTransform * glm::vec4(instance.position, 1.0f));
            glm::mat4 model(1.0f);
            model[0] = glm::vec4(SafeNormalizedAxis(glm::vec3(ownerTransform[0]), glm::vec3(1.0f, 0.0f, 0.0f)), 0.0f);
            model[1] = glm::vec4(SafeNormalizedAxis(glm::vec3(ownerTransform[1]), glm::vec3(0.0f, 1.0f, 0.0f)), 0.0f);
            model[2] = glm::vec4(SafeNormalizedAxis(glm::vec3(ownerTransform[2]), glm::vec3(0.0f, 0.0f, 1.0f)), 0.0f);
            model[3] = glm::vec4(worldPosition, 1.0f);
            model = glm::rotate(model, glm::radians(instance.rotationDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::rotate(model, glm::radians(instance.rotationDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
            model = glm::rotate(model, glm::radians(instance.rotationDegrees.z), glm::vec3(0.0f, 0.0f, 1.0f));
            if (applyInstanceScale)
            {
                model = glm::scale(model, instance.scale);
            }
            model = glm::translate(model, -localOriginOffset);
            return model;
        }

        render::MeshBounds TransformBounds(const render::MeshBounds &bounds, const glm::mat4 &model)
        {
            const glm::vec3 worldCenter = glm::vec3(model * glm::vec4(bounds.center, 1.0f));
            const float scaleX = glm::length(glm::vec3(model[0]));
            const float scaleY = glm::length(glm::vec3(model[1]));
            const float scaleZ = glm::length(glm::vec3(model[2]));
            return render::MeshBounds{.center = worldCenter, .radius = bounds.radius * (std::max)(scaleX, (std::max)(scaleY, scaleZ))};
        }

        struct FoliageClusterKey
        {
            int x = 0;
            int z = 0;

            bool operator==(const FoliageClusterKey &other) const
            {
                return x == other.x && z == other.z;
            }
        };

        struct FoliageClusterKeyHash
        {
            std::size_t operator()(const FoliageClusterKey &key) const
            {
                const std::size_t hx = std::hash<int>{}(key.x);
                const std::size_t hz = std::hash<int>{}(key.z);
                return hx ^ (hz + 0x9e3779b97f4a7c15ull + (hx << 6) + (hx >> 2));
            }
        };

        struct FoliageCluster
        {
            std::shared_ptr<std::vector<glm::mat4>> models = std::make_shared<std::vector<glm::mat4>>();
            render::MeshBounds bounds{};
        };

        render::MeshBounds ComputeClusterBounds(const std::vector<glm::mat4> &models, const render::MeshBounds &meshBounds)
        {
            if (models.empty())
            {
                return meshBounds;
            }

            glm::vec3 minPoint(std::numeric_limits<float>::max());
            glm::vec3 maxPoint(-std::numeric_limits<float>::max());
            float maxRadius = 0.0f;
            for (const auto &model : models)
            {
                const auto transformed = TransformBounds(meshBounds, model);
                minPoint = glm::min(minPoint, transformed.center - glm::vec3(transformed.radius));
                maxPoint = glm::max(maxPoint, transformed.center + glm::vec3(transformed.radius));
                maxRadius = (std::max)(maxRadius, transformed.radius);
            }

            const glm::vec3 center = (minPoint + maxPoint) * 0.5f;
            float radius = maxRadius;
            for (const auto &model : models)
            {
                const auto transformed = TransformBounds(meshBounds, model);
                radius = (std::max)(radius, glm::length(transformed.center - center) + transformed.radius);
            }

            return render::MeshBounds{.center = center, .radius = radius};
        }

        glm::vec3 GetVertexPosition(const render::MeshVertexData &vertex)
        {
            return glm::vec3(vertex.position[0], vertex.position[1], vertex.position[2]);
        }

        glm::vec3 ComputeSubmeshCenterOfMass(const render::Mesh &mesh, std::size_t submeshIndex)
        {
            if (submeshIndex >= mesh.GetSubmeshCount())
            {
                return glm::vec3(0.0f);
            }

            const auto &submesh = mesh.GetSubmesh(submeshIndex);
            const auto &meshData = mesh.GetMeshData();
            if (submesh.indexCount == 0 ||
                submesh.indexOffset + submesh.indexCount > meshData.indices.size() ||
                meshData.vertices.empty())
            {
                return submesh.bounds.center;
            }

            glm::vec3 weightedCenter(0.0f);
            float totalArea = 0.0f;
            for (uint32_t index = submesh.indexOffset; index + 2 < submesh.indexOffset + submesh.indexCount; index += 3)
            {
                const auto i0 = meshData.indices[index];
                const auto i1 = meshData.indices[index + 1];
                const auto i2 = meshData.indices[index + 2];
                if (i0 >= meshData.vertices.size() || i1 >= meshData.vertices.size() || i2 >= meshData.vertices.size())
                {
                    continue;
                }

                const glm::vec3 p0 = GetVertexPosition(meshData.vertices[i0]);
                const glm::vec3 p1 = GetVertexPosition(meshData.vertices[i1]);
                const glm::vec3 p2 = GetVertexPosition(meshData.vertices[i2]);
                const float area = glm::length(glm::cross(p1 - p0, p2 - p0)) * 0.5f;
                if (area <= 0.0f)
                {
                    continue;
                }

                weightedCenter += ((p0 + p1 + p2) / 3.0f) * area;
                totalArea += area;
            }

            if (totalArea > 0.0f)
            {
                return weightedCenter / totalArea;
            }

            glm::vec3 average(0.0f);
            std::size_t count = 0;
            for (uint32_t index = submesh.indexOffset; index < submesh.indexOffset + submesh.indexCount; ++index)
            {
                const auto vertexIndex = meshData.indices[index];
                if (vertexIndex >= meshData.vertices.size())
                {
                    continue;
                }
                average += GetVertexPosition(meshData.vertices[vertexIndex]);
                ++count;
            }

            return count > 0 ? average / static_cast<float>(count) : submesh.bounds.center;
        }

        std::vector<std::size_t> ResolveSelectedSubmeshes(const FoliageType &type)
        {
            std::vector<std::size_t> selectedSubmeshes;
            if (!type.mesh)
            {
                return selectedSubmeshes;
            }

            const std::size_t submeshCount = type.mesh->GetSubmeshCount();
            if (!type.submeshIndices.empty())
            {
                selectedSubmeshes.reserve(type.submeshIndices.size());
                for (const int submeshIndex : type.submeshIndices)
                {
                    if (submeshIndex >= 0 && static_cast<std::size_t>(submeshIndex) < submeshCount)
                    {
                        const auto resolved = static_cast<std::size_t>(submeshIndex);
                        if (std::find(selectedSubmeshes.begin(), selectedSubmeshes.end(), resolved) == selectedSubmeshes.end())
                        {
                            selectedSubmeshes.push_back(resolved);
                        }
                    }
                }
            }
            else if (type.submeshIndex >= 0 && static_cast<std::size_t>(type.submeshIndex) < submeshCount)
            {
                selectedSubmeshes.push_back(static_cast<std::size_t>(type.submeshIndex));
            }

            if (selectedSubmeshes.empty())
            {
                selectedSubmeshes.reserve(submeshCount);
                for (std::size_t submeshIndex = 0; submeshIndex < submeshCount; ++submeshIndex)
                {
                    selectedSubmeshes.push_back(submeshIndex);
                }
            }

            return selectedSubmeshes;
        }

        glm::vec3 ComputeSubmeshGroupCenterOfMass(const render::Mesh &mesh, const std::vector<std::size_t> &submeshIndices)
        {
            if (submeshIndices.empty() || submeshIndices.size() == mesh.GetSubmeshCount())
            {
                return glm::vec3(0.0f);
            }

            glm::vec3 weightedCenter(0.0f);
            float totalWeight = 0.0f;
            for (const std::size_t submeshIndex : submeshIndices)
            {
                if (submeshIndex >= mesh.GetSubmeshCount())
                {
                    continue;
                }

                const auto &submesh = mesh.GetSubmesh(submeshIndex);
                const float weight = (std::max)(1.0f, submesh.bounds.radius * submesh.bounds.radius);
                weightedCenter += ComputeSubmeshCenterOfMass(mesh, submeshIndex) * weight;
                totalWeight += weight;
            }

            return totalWeight > 0.0f ? weightedCenter / totalWeight : glm::vec3(0.0f);
        }

        render::MeshBounds CombineSubmeshBounds(const render::Mesh &mesh, const std::vector<std::size_t> &submeshIndices)
        {
            if (submeshIndices.empty() || submeshIndices.size() == mesh.GetSubmeshCount())
            {
                return mesh.GetBounds();
            }

            glm::vec3 minPoint(std::numeric_limits<float>::max());
            glm::vec3 maxPoint(-std::numeric_limits<float>::max());
            bool hasBounds = false;
            for (const std::size_t submeshIndex : submeshIndices)
            {
                if (submeshIndex >= mesh.GetSubmeshCount())
                {
                    continue;
                }

                const auto &bounds = mesh.GetSubmesh(submeshIndex).bounds;
                minPoint = glm::min(minPoint, bounds.center - glm::vec3(bounds.radius));
                maxPoint = glm::max(maxPoint, bounds.center + glm::vec3(bounds.radius));
                hasBounds = true;
            }

            if (!hasBounds)
            {
                return mesh.GetBounds();
            }

            const glm::vec3 center = (minPoint + maxPoint) * 0.5f;
            float radius = 0.0f;
            for (const std::size_t submeshIndex : submeshIndices)
            {
                if (submeshIndex >= mesh.GetSubmeshCount())
                {
                    continue;
                }
                const auto &bounds = mesh.GetSubmesh(submeshIndex).bounds;
                radius = (std::max)(radius, glm::length(bounds.center - center) + bounds.radius);
            }

            return render::MeshBounds{.center = center, .radius = radius};
        }

        void SanitizeTypeSubmeshSelection(FoliageType &type)
        {
            if (!type.mesh)
            {
                type.submeshIndex = -1;
                type.submeshIndices.clear();
                return;
            }

            const int maxSubmeshIndex = static_cast<int>((std::max<std::size_t>)(type.mesh->GetSubmeshCount(), 1) - 1);
            if (type.submeshIndex > maxSubmeshIndex)
            {
                type.submeshIndex = -1;
            }

            std::vector<int> sanitizedIndices;
            sanitizedIndices.reserve(type.submeshIndices.size());
            for (const int submeshIndex : type.submeshIndices)
            {
                if (submeshIndex < 0 || submeshIndex > maxSubmeshIndex)
                {
                    continue;
                }
                if (std::find(sanitizedIndices.begin(), sanitizedIndices.end(), submeshIndex) == sanitizedIndices.end())
                {
                    sanitizedIndices.push_back(submeshIndex);
                }
            }

            type.submeshIndices = std::move(sanitizedIndices);
            if (!type.submeshIndices.empty())
            {
                type.submeshIndex = type.submeshIndices.size() == 1 ? type.submeshIndices.front() : -1;
            }
        }

        void AssignFoliageMaterials(FoliageType &type, const std::vector<render::Material *> &materials)
        {
            // The caller may pass type.materials itself (or another view containing pointers
            // owned by type). Snapshot every config before releasing the old ownership;
            // otherwise clearing ownedMaterials leaves the input vector full of dangling
            // pointers and the first GetConfig() below becomes a use-after-free.
            std::vector<std::optional<render::MaterialConfig>> materialConfigs;
            materialConfigs.reserve(materials.size());
            for (const auto *material : materials)
            {
                materialConfigs.emplace_back(material ? std::optional<render::MaterialConfig>(material->GetConfig()) : std::nullopt);
            }

            type.ownedMaterials.clear();
            type.materials.clear();
            type.ownedMaterials.reserve(materialConfigs.size());
            type.materials.reserve(materialConfigs.size());

            for (auto &materialConfig : materialConfigs)
            {
                if (!materialConfig)
                {
                    type.materials.push_back(nullptr);
                    continue;
                }

                auto &config = *materialConfig;
                if (config.alphaMode == render::AlphaMode::Blend && config.albedoTexture)
                {
                    config.alphaMode = render::AlphaMode::Mask;
                    config.alphaCutoff = (std::max)(config.alphaCutoff, 0.35f);
                }

                type.ownedMaterials.push_back(std::make_unique<render::Material>(config));
                type.materials.push_back(type.ownedMaterials.back().get());
            }
        }

        std::string DefaultTypeName(std::size_t index)
        {
            return "Foliage " + std::to_string(index + 1);
        }

        bool AreMatricesApproximatelyEqual(const glm::mat4 &a, const glm::mat4 &b, float epsilon = 0.0001f)
        {
            for (int column = 0; column < 4; ++column)
            {
                for (int row = 0; row < 4; ++row)
                {
                    if (std::abs(a[column][row] - b[column][row]) > epsilon)
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        bool CompareRenderCommandKeys(const render::RenderCommand &a, const render::RenderCommand &b)
        {
            auto *aShader = a.material ? a.material->GetShader() : a.shader;
            auto *bShader = b.material ? b.material->GetShader() : b.shader;
            if (aShader != bShader)
            {
                return aShader < bShader;
            }
            if (a.material != b.material)
            {
                return a.material < b.material;
            }
            if (a.mesh != b.mesh)
            {
                return a.mesh < b.mesh;
            }

            const auto aRange = a.mesh ? a.mesh->GetSubmeshLodRange(a.submeshIndex, a.lodIndex) : render::Submesh::LodRange{};
            const auto bRange = b.mesh ? b.mesh->GetSubmeshLodRange(b.submeshIndex, b.lodIndex) : render::Submesh::LodRange{};
            if (aRange.indexOffset != bRange.indexOffset)
            {
                return aRange.indexOffset < bRange.indexOffset;
            }
            if (aRange.indexCount != bRange.indexCount)
            {
                return aRange.indexCount < bRange.indexCount;
            }
            if (a.submeshIndex != b.submeshIndex)
            {
                return a.submeshIndex < b.submeshIndex;
            }
            return a.lodIndex < b.lodIndex;
        }
    }

    std::size_t FoliageCellCoordinateHash::operator()(const FoliageCellCoordinate &coordinate) const noexcept
    {
        const std::size_t hx = std::hash<int>{}(coordinate.x);
        const std::size_t hz = std::hash<int>{}(coordinate.z);
        return hx ^ (hz + 0x9e3779b97f4a7c15ull + (hx << 6) + (hx >> 2));
    }

    void FoliageComponent::Update(float deltaTime)
    {
        (void)deltaTime;
    }

    void FoliageComponent::SubmitRenderCommands()
    {
        auto *owner = GetOwner();
        if (!owner)
        {
            return;
        }

        const glm::mat4 ownerTransform = owner->GetWorldTransform();
        if (m_renderCommandCacheDirty ||
            !m_hasCachedRenderCommandModel ||
            !AreMatricesApproximatelyEqual(m_cachedRenderCommandModel, ownerTransform))
        {
            RebuildRenderCommandCache(ownerTransform);
        }

        if (m_cachedRenderCommands.empty())
        {
            return;
        }

        core::Engine::GetInstance().GetRenderer().SubmitSortedRenderCommands(m_cachedRenderCommands, true);
    }

    std::vector<std::size_t> FoliageComponent::GetBakeSubmeshes(std::size_t typeIndex) const
    {
        const auto *type = GetType(typeIndex);
        return type ? ResolveSelectedSubmeshes(*type) : std::vector<std::size_t>{};
    }

    render::Material *FoliageComponent::GetBakeMaterial(std::size_t typeIndex, std::size_t submeshIndex) const
    {
        const auto *type = GetType(typeIndex);
        return type ? GetMaterialForTypeSubmesh(*type, submeshIndex) : nullptr;
    }

    glm::mat4 FoliageComponent::GetBakeInstanceTransform(std::size_t typeIndex, std::size_t instanceIndex) const
    {
        const auto *type = GetType(typeIndex);
        const auto *owner = GetOwner();
        if (!type || !type->mesh || !owner || instanceIndex >= type->instances.size())
            return glm::mat4(1.0f);
        const auto selectedSubmeshes = ResolveSelectedSubmeshes(*type);
        const glm::vec3 localOriginOffset = ComputeSubmeshGroupCenterOfMass(*type->mesh, selectedSubmeshes);
        return ComposeInstanceTransform(owner->GetWorldTransform(), type->instances[instanceIndex], localOriginOffset);
    }

    void FoliageComponent::RebuildRenderCommandCache(const glm::mat4 &ownerTransform)
    {
        m_cachedRenderCommands.clear();
        for (const auto &type : m_types)
        {
            if (!type.mesh || type.instances.empty())
            {
                continue;
            }

            const auto selectedSubmeshes = ResolveSelectedSubmeshes(type);
            if (selectedSubmeshes.empty())
            {
                continue;
            }

            const glm::vec3 localOriginOffset = ComputeSubmeshGroupCenterOfMass(*type.mesh, selectedSubmeshes);
            const render::MeshBounds clusterSourceBounds = CombineSubmeshBounds(*type.mesh, selectedSubmeshes);

            std::unordered_map<FoliageClusterKey, FoliageCluster, FoliageClusterKeyHash> clusters;
            for (const auto &instance : type.instances)
            {
                const glm::mat4 model = ComposeInstanceTransform(ownerTransform, instance, localOriginOffset);
                const FoliageClusterKey key{
                    .x = GetCellCoordinate(type, instance.position).x,
                    .z = GetCellCoordinate(type, instance.position).z,
                };
                clusters[key].models->push_back(model);
            }

            for (auto &[key, cluster] : clusters)
            {
                (void)key;
                if (!cluster.models || cluster.models->empty())
                {
                    continue;
                }

                cluster.bounds = ComputeClusterBounds(*cluster.models, clusterSourceBounds);
                for (const std::size_t submeshIndex : selectedSubmeshes)
                {
                    auto *material = GetMaterialForTypeSubmesh(type, submeshIndex);
                    if (!material)
                    {
                        continue;
                    }

                    render::RenderCommand command;
                    command.model = cluster.models->front();
                    command.previousModel = command.model;
                    command.material = material;
                    command.mesh = type.mesh;
                    command.shader = material->GetShader();
                    command.worldBounds = cluster.bounds;
                    command.previousWorldBounds = command.worldBounds;
                    command.instanceModels = cluster.models;
                    command.previousInstanceModels = cluster.models;
                    command.submeshIndex = static_cast<uint32_t>(submeshIndex);
                    command.minLodIndex = static_cast<uint32_t>((std::max)(0, m_minRenderLod));
                    command.minShadowLodIndex = static_cast<uint32_t>((std::max)(0, m_minShadowLod));
                    command.maxDrawDistance = type.asset.maxDrawDistance > 0.0f
                                                  ? type.asset.maxDrawDistance
                                                  : m_maxDrawDistance;
                    command.maxShadowDistance = m_maxShadowDistance;
                    command.isStatic = m_isStatic;
                    command.castsShadow = m_castShadows;
                    command.usePrimaryUvForLightmap = true;
                    m_cachedRenderCommands.push_back(command);
                }
            }
        }

        std::sort(m_cachedRenderCommands.begin(), m_cachedRenderCommands.end(), CompareRenderCommandKeys);
        m_cachedRenderCommandModel = ownerTransform;
        m_hasCachedRenderCommandModel = true;
        m_renderCommandCacheDirty = false;
    }

    std::vector<Property> FoliageComponent::Serialize() const
    {
        std::vector<Property> properties = {
            {"PaintEnabled", PropertyType::Bool, m_paintEnabled ? "true" : "false"},
            {"BrushMode", PropertyType::String, m_brushMode == FoliageBrushMode::Remove ? "Remove" : "Add"},
            {"BrushRadius", PropertyType::Float, std::to_string(m_brushRadius)},
            {"Density", PropertyType::Int, std::to_string(m_density)},
            {"MinScale", PropertyType::Float, std::to_string(m_minScale)},
            {"MaxScale", PropertyType::Float, std::to_string(m_maxScale)},
            {"MaxDrawDistance", PropertyType::Float, std::to_string(m_maxDrawDistance)},
            {"MaxShadowDistance", PropertyType::Float, std::to_string(m_maxShadowDistance)},
            {"MinRenderLod", PropertyType::Int, std::to_string(m_minRenderLod)},
            {"MinShadowLod", PropertyType::Int, std::to_string(m_minShadowLod)},
            {"CastShadows", PropertyType::Bool, m_castShadows ? "true" : "false"},
            {"Static", PropertyType::Bool, m_isStatic ? "true" : "false"},
            {"SelectedType", PropertyType::Int, std::to_string(m_selectedTypeIndex)},
            {"FoliageTypeCount", PropertyType::Int, std::to_string(m_types.size())},
        };

        for (std::size_t index = 0; index < m_types.size(); ++index)
        {
            const auto &type = m_types[index];
            const std::string prefix = std::string(kTypePrefix) + std::to_string(index) + ".";
            properties.push_back({prefix + "Name", PropertyType::String, type.name});
            properties.push_back({prefix + "SourceMesh", PropertyType::String, type.sourceMeshPath});
            properties.push_back({prefix + "MaterialAsset", PropertyType::String, type.materialAssetReference});
            properties.push_back({prefix + "SubmeshIndex", PropertyType::Int, std::to_string(type.submeshIndex)});
            properties.push_back({prefix + "SubmeshIndices", PropertyType::String, SerializeSubmeshIndices(type.submeshIndices)});
            properties.push_back({prefix + "UseGeneratedLods", PropertyType::Bool, type.useGeneratedLods ? "true" : "false"});
            properties.push_back({prefix + "AssetReference", PropertyType::String, type.asset.assetReference});
            properties.push_back({prefix + "CellSize", PropertyType::Float, std::to_string(type.asset.cellSize)});
            properties.push_back({prefix + "MaxDrawDistance", PropertyType::Float, std::to_string(type.asset.maxDrawDistance)});
            properties.push_back({prefix + "CollisionEnabled", PropertyType::Bool, type.asset.collisionEnabled ? "true" : "false"});
            properties.push_back({prefix + "CollisionCenter", PropertyType::Vec3,
                                  std::to_string(type.asset.collisionCenter.x) + "," +
                                  std::to_string(type.asset.collisionCenter.y) + "," +
                                  std::to_string(type.asset.collisionCenter.z)});
            properties.push_back({prefix + "CollisionRadius", PropertyType::Float, std::to_string(type.asset.collisionRadius)});
            properties.push_back({prefix + "CollisionHeight", PropertyType::Float, std::to_string(type.asset.collisionHeight)});
            properties.push_back({prefix + "Instances", PropertyType::String, SerializeInstances(type.instances)});
        }

        return properties;
    }

    void FoliageComponent::Deserialize(const std::vector<Property> &properties)
    {
        std::unordered_map<std::size_t, FoliageType> parsedTypes;
        std::string legacySourceMeshPath;
        std::string legacyMaterialAssetReference;
        std::string legacyInstances;
        int typeCount = 0;

        for (const auto &property : properties)
        {
            if (property.name == "PaintEnabled")
                m_paintEnabled = property.value == "true" || property.value == "1";
            else if (property.name == "BrushMode")
                m_brushMode = property.value == "Remove" ? FoliageBrushMode::Remove : FoliageBrushMode::Add;
            else if (property.name == "BrushRadius")
                SetBrushRadius(std::stof(property.value));
            else if (property.name == "Density")
                SetDensity(std::stoi(property.value));
            else if (property.name == "MinScale")
                m_minScale = (std::max)(0.01f, std::stof(property.value));
            else if (property.name == "MaxScale")
                m_maxScale = (std::max)(0.01f, std::stof(property.value));
            else if (property.name == "MaxDrawDistance")
                SetMaxDrawDistance(std::stof(property.value));
            else if (property.name == "MaxShadowDistance")
                SetMaxShadowDistance(std::stof(property.value));
            else if (property.name == "MinRenderLod")
                SetMinRenderLod(std::stoi(property.value));
            else if (property.name == "MinShadowLod")
                SetMinShadowLod(std::stoi(property.value));
            else if (property.name == "CastShadows")
                SetCastShadows(property.value == "true" || property.value == "1");
            else if (property.name == "Static")
                SetStatic(property.value == "true" || property.value == "1");
            else if (property.name == "SelectedType")
                m_selectedTypeIndex = std::stoi(property.value);
            else if (property.name == "FoliageTypeCount")
                typeCount = (std::max)(0, std::stoi(property.value));
            else if (property.name == "SourceMesh")
                legacySourceMeshPath = property.value;
            else if (property.name == "MaterialAsset")
                legacyMaterialAssetReference = property.value;
            else if (property.name == "Instances")
                legacyInstances = property.value;
            else if (property.name.rfind(kTypePrefix, 0) == 0)
            {
                const std::string remainder = property.name.substr(std::char_traits<char>::length(kTypePrefix));
                const auto separator = remainder.find('.');
                if (separator == std::string::npos)
                {
                    continue;
                }

                const std::size_t typeIndex = static_cast<std::size_t>(std::stoul(remainder.substr(0, separator)));
                const std::string fieldName = remainder.substr(separator + 1);
                auto &type = parsedTypes[typeIndex];
                if (type.name.empty())
                {
                    type.name = DefaultTypeName(typeIndex);
                }
                if (fieldName == "Name")
                    type.name = property.value.empty() ? DefaultTypeName(typeIndex) : property.value;
                else if (fieldName == "SourceMesh")
                    type.sourceMeshPath = property.value;
                else if (fieldName == "MaterialAsset")
                    type.materialAssetReference = property.value;
                else if (fieldName == "SubmeshIndex")
                    type.submeshIndex = std::stoi(property.value);
                else if (fieldName == "SubmeshIndices")
                    type.submeshIndices = DeserializeSubmeshIndices(property.value);
                else if (fieldName == "UseGeneratedLods")
                    type.useGeneratedLods = property.value == "true" || property.value == "1";
                else if (fieldName == "AssetReference")
                    type.asset.assetReference = property.value;
                else if (fieldName == "CellSize")
                    type.asset.cellSize = (std::max)(1.0f, std::stof(property.value));
                else if (fieldName == "MaxDrawDistance")
                    type.asset.maxDrawDistance = (std::max)(0.0f, std::stof(property.value));
                else if (fieldName == "CollisionEnabled")
                    type.asset.collisionEnabled = property.value == "true" || property.value == "1";
                else if (fieldName == "CollisionCenter")
                {
                    std::stringstream centerStream(property.value);
                    char separator = 0;
                    centerStream >> type.asset.collisionCenter.x >> separator
                                 >> type.asset.collisionCenter.y >> separator
                                 >> type.asset.collisionCenter.z;
                }
                else if (fieldName == "CollisionRadius")
                    type.asset.collisionRadius = (std::max)(0.01f, std::stof(property.value));
                else if (fieldName == "CollisionHeight")
                    type.asset.collisionHeight = (std::max)(type.asset.collisionRadius * 2.0f, std::stof(property.value));
                else if (fieldName == "Instances")
                    type.instances = DeserializeInstances(property.value);
            }
        }

        m_types.clear();
        if (!parsedTypes.empty() || typeCount > 0)
        {
            std::size_t highestParsedType = 0;
            for (const auto &[typeIndex, type] : parsedTypes)
            {
                (void)type;
                highestParsedType = (std::max)(highestParsedType, typeIndex + 1);
            }
            const std::size_t finalTypeCount = (std::max)(static_cast<std::size_t>(typeCount), highestParsedType);
            m_types.resize(finalTypeCount);
            for (std::size_t index = 0; index < m_types.size(); ++index)
            {
                auto found = parsedTypes.find(index);
                m_types[index] = found != parsedTypes.end() ? std::move(found->second) : FoliageType{.name = DefaultTypeName(index)};
                if (m_types[index].name.empty())
                {
                    m_types[index].name = DefaultTypeName(index);
                }
            }
        }
        else
        {
            FoliageType legacyType;
            legacyType.name = "Foliage 1";
            legacyType.sourceMeshPath = legacySourceMeshPath;
            legacyType.materialAssetReference = legacyMaterialAssetReference;
            legacyType.instances = DeserializeInstances(legacyInstances);
            m_types.push_back(std::move(legacyType));
        }

        if (m_maxScale < m_minScale)
        {
            std::swap(m_minScale, m_maxScale);
        }

        EnsureTypeStorage();
        m_selectedTypeIndex = std::clamp(m_selectedTypeIndex, 0, static_cast<int>(m_types.size()) - 1);
        for (auto &type : m_types)
        {
            if (!type.asset.assetReference.empty())
            {
                const std::string reference = type.asset.assetReference;
                LoadFoliageTypeAsset(core::Engine::GetInstance().GetAssetManager().ResolveAssetPath(reference), type.asset);
                type.asset.assetReference = reference;
            }
            RebuildTypeMeshFromReference(type);
            RebuildTypeMaterialFromReference(type);
        }
        EnsureStableInstanceIds();
        // Deserialization is also used when restoring editor state. Advance the
        // instance revision so an existing Bullet cache cannot keep the old (or
        // empty) collider set for the newly restored placements.
        MarkInstancesDirty();
    }

    void FoliageComponent::SetMesh(render::Mesh *mesh)
    {
        EnsureSelectedType().mesh = mesh;
        MarkRenderCommandsDirty();
    }

    render::Mesh *FoliageComponent::GetMesh() const
    {
        const auto *type = GetSelectedType();
        return type ? type->mesh : nullptr;
    }

    void FoliageComponent::SetSourceMeshPath(const std::string &sourceMeshPath)
    {
        SetTypeSourceMeshPath(static_cast<std::size_t>(m_selectedTypeIndex), sourceMeshPath);
    }

    const std::string &FoliageComponent::GetSourceMeshPath() const
    {
        static const std::string empty;
        const auto *type = GetSelectedType();
        return type ? type->sourceMeshPath : empty;
    }

    void FoliageComponent::SetMaterialAssetReference(const std::string &materialAssetReference)
    {
        SetTypeMaterialAssetReference(static_cast<std::size_t>(m_selectedTypeIndex), materialAssetReference);
    }

    const std::string &FoliageComponent::GetMaterialAssetReference() const
    {
        static const std::string empty;
        const auto *type = GetSelectedType();
        return type ? type->materialAssetReference : empty;
    }

    void FoliageComponent::ClearMaterialAssetReference()
    {
        ClearTypeMaterialAssetReference(static_cast<std::size_t>(m_selectedTypeIndex));
    }

    void FoliageComponent::SetBrushRadius(float radius)
    {
        m_brushRadius = (std::max)(radius, 0.05f);
    }

    void FoliageComponent::SetDensity(int density)
    {
        m_density = std::clamp(density, 1, 100);
    }

    void FoliageComponent::SetScaleRange(float minScale, float maxScale)
    {
        m_minScale = (std::max)(0.0001f, (std::min)(minScale, maxScale));
        m_maxScale = (std::max)(m_minScale, (std::max)(minScale, maxScale));
    }

    void FoliageComponent::SetMaxDrawDistance(float distance)
    {
        m_maxDrawDistance = distance <= 0.0f ? std::numeric_limits<float>::max() : (std::max)(distance, 1.0f);
        MarkRenderCommandsDirty();
    }

    void FoliageComponent::SetMaxShadowDistance(float distance)
    {
        m_maxShadowDistance = distance <= 0.0f ? std::numeric_limits<float>::max() : (std::max)(distance, 1.0f);
        MarkRenderCommandsDirty();
    }

    void FoliageComponent::SetMinRenderLod(int lodIndex)
    {
        m_minRenderLod = std::clamp(lodIndex, 0, 8);
        MarkRenderCommandsDirty();
    }

    void FoliageComponent::SetMinShadowLod(int lodIndex)
    {
        m_minShadowLod = std::clamp(lodIndex, 0, 8);
        MarkRenderCommandsDirty();
    }

    void FoliageComponent::SetCastShadows(bool castShadows)
    {
        m_castShadows = castShadows;
        MarkRenderCommandsDirty();
    }

    bool FoliageComponent::ApplyBrushAtWorldPosition(const glm::vec3 &worldPosition,
                                                     const glm::vec3 &terrainNormal,
                                                     const std::function<float(float, float)> &sampleTerrainHeight)
    {
        if (m_brushMode == FoliageBrushMode::Add)
        {
            return PaintAtWorldPosition(worldPosition, terrainNormal, sampleTerrainHeight);
        }
        else if (m_brushMode == FoliageBrushMode::Remove)
        {
            return RemoveInstancesAtWorldPosition(worldPosition, m_brushRadius);
        }
        return false;
    }

    bool FoliageComponent::PaintAtWorldPosition(const glm::vec3 &worldPosition,
                                                const glm::vec3 &terrainNormal,
                                                const std::function<float(float, float)> &sampleTerrainHeight)
    {
        auto *owner = GetOwner();
        auto &type = EnsureSelectedType();
        if (!owner || !m_paintEnabled || !type.mesh)
        {
            return false;
        }

        const glm::mat4 inverseWorld = glm::inverse(owner->GetWorldTransform());
        const glm::vec3 localCenter = glm::vec3(inverseWorld * glm::vec4(worldPosition, 1.0f));
        std::seed_seq seed{
            static_cast<unsigned int>(std::lround(worldPosition.x * 100.0f)),
            static_cast<unsigned int>(std::lround(worldPosition.z * 100.0f)),
            static_cast<unsigned int>(type.instances.size()),
            static_cast<unsigned int>(m_selectedTypeIndex),
        };
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> angleDistribution(0.0f, glm::two_pi<float>());
        std::uniform_real_distribution<float> radiusDistribution(0.0f, 1.0f);
        std::uniform_real_distribution<float> yawDistribution(0.0f, 360.0f);
        std::uniform_real_distribution<float> scaleDistribution(m_minScale, m_maxScale);

        const auto startCount = type.instances.size();
        type.instances.reserve(type.instances.size() + static_cast<std::size_t>(m_density));
        for (int index = 0; index < m_density; ++index)
        {
            const float angle = angleDistribution(rng);
            const float radius = std::sqrt(radiusDistribution(rng)) * m_brushRadius;
            glm::vec3 localPosition = localCenter + glm::vec3(std::cos(angle) * radius, 0.0f, std::sin(angle) * radius);
            localPosition.y = sampleTerrainHeight ? sampleTerrainHeight(localPosition.x, localPosition.z) : localCenter.y;
            const float scale = scaleDistribution(rng);
            const glm::vec3 normal = glm::normalize(terrainNormal);
            type.instances.push_back(FoliageInstance{
                .id = m_nextInstanceId++,
                .position = localPosition,
                .rotationDegrees = {glm::degrees(std::atan2(normal.z, normal.y)), yawDistribution(rng), -glm::degrees(std::atan2(normal.x, normal.y))},
                .scale = {scale, scale, scale},
            });
        }
        const bool changed = type.instances.size() != startCount;
        if (changed)
        {
            MarkInstancesDirty();
        }
        return changed;
    }

    bool FoliageComponent::RemoveInstancesAtWorldPosition(const glm::vec3 &worldPosition, float radius)
    {
        auto *owner = GetOwner();
        auto &type = EnsureSelectedType();
        if (!owner || !type.mesh || radius <= 0.0f)
        {
            return false;
        }

        const glm::mat4 inverseWorld = glm::inverse(owner->GetWorldTransform());
        const glm::vec3 localCenter = glm::vec3(inverseWorld * glm::vec4(worldPosition, 1.0f));
        const float radiusSquared = radius * radius;

        const auto startCount = type.instances.size();
        type.instances.erase(std::remove_if(type.instances.begin(), type.instances.end(),
                                            [&](const FoliageInstance &instance)
                                            {
                                                return glm::distance2(instance.position, localCenter) <= radiusSquared;
                                            }),
                             type.instances.end());
        const bool changed = type.instances.size() != startCount;
        if (changed)
        {
            MarkInstancesDirty();
        }
        return changed;
    }

    void FoliageComponent::ClearInstances()
    {
        for (auto &type : m_types)
        {
            type.instances.clear();
        }
        MarkInstancesDirty();
    }

    void FoliageComponent::ClearSelectedTypeInstances()
    {
        EnsureSelectedType().instances.clear();
        MarkInstancesDirty();
    }

    const std::vector<FoliageInstance> &FoliageComponent::GetInstances() const
    {
        const auto *type = GetSelectedType();
        return type ? type->instances : m_emptyInstances;
    }

    FoliageInstance *FoliageComponent::GetSelectedTypeInstance(std::size_t instanceIndex)
    {
        auto *type = GetSelectedType();
        return type && instanceIndex < type->instances.size() ? &type->instances[instanceIndex] : nullptr;
    }

    const FoliageInstance *FoliageComponent::GetSelectedTypeInstance(std::size_t instanceIndex) const
    {
        const auto *type = GetSelectedType();
        return type && instanceIndex < type->instances.size() ? &type->instances[instanceIndex] : nullptr;
    }

    bool FoliageComponent::SetSelectedTypeInstanceTransform(std::size_t instanceIndex,
                                                            const glm::vec3 &position,
                                                            const glm::vec3 &rotationDegrees,
                                                            const glm::vec3 &scale)
    {
        auto *instance = GetSelectedTypeInstance(instanceIndex);
        if (!instance)
        {
            return false;
        }

        const glm::vec3 sanitizedScale{
            (std::max)(scale.x, 0.0001f),
            (std::max)(scale.y, 0.0001f),
            (std::max)(scale.z, 0.0001f),
        };
        if (instance->position == position &&
            instance->rotationDegrees == rotationDegrees &&
            instance->scale == sanitizedScale)
        {
            return false;
        }

        instance->position = position;
        instance->rotationDegrees = rotationDegrees;
        instance->scale = sanitizedScale;
        MarkInstancesDirty();
        return true;
    }

    std::size_t FoliageComponent::SetSelectedTypeInstancesScale(const glm::vec3 &scale)
    {
        auto *type = GetSelectedType();
        if (!type)
        {
            return 0;
        }

        const glm::vec3 sanitizedScale{
            (std::max)(scale.x, 0.0001f),
            (std::max)(scale.y, 0.0001f),
            (std::max)(scale.z, 0.0001f),
        };
        std::size_t changedCount = 0;
        for (auto &instance : type->instances)
        {
            if (instance.scale != sanitizedScale)
            {
                instance.scale = sanitizedScale;
                ++changedCount;
            }
        }
        if (changedCount > 0)
        {
            MarkInstancesDirty();
        }
        return changedCount;
    }

    std::size_t FoliageComponent::SnapSelectedTypeInstancesToSurface(
        const std::function<float(float, float)> &sampleTerrainHeight)
    {
        auto *type = GetSelectedType();
        if (!type || !sampleTerrainHeight)
        {
            return 0;
        }

        std::size_t changedCount = 0;
        for (auto &instance : type->instances)
        {
            const float height = sampleTerrainHeight(instance.position.x, instance.position.z);
            if (instance.position.y != height)
            {
                instance.position.y = height;
                ++changedCount;
            }
        }

        if (changedCount > 0)
        {
            MarkInstancesDirty();
        }
        return changedCount;
    }

    std::size_t FoliageComponent::SnapAllInstancesToSurface(
        const std::function<float(float, float)> &sampleTerrainHeight)
    {
        if (!sampleTerrainHeight)
        {
            return 0;
        }

        std::size_t changedCount = 0;
        for (auto &type : m_types)
        {
            for (auto &instance : type.instances)
            {
                const float height = sampleTerrainHeight(instance.position.x, instance.position.z);
                if (instance.position.y != height)
                {
                    instance.position.y = height;
                    ++changedCount;
                }
            }
        }

        if (changedCount > 0)
        {
            MarkInstancesDirty();
        }
        return changedCount;
    }

    bool FoliageComponent::RemoveSelectedTypeInstance(std::size_t instanceIndex)
    {
        auto *type = GetSelectedType();
        if (!type || instanceIndex >= type->instances.size())
        {
            return false;
        }

        type->instances.erase(type->instances.begin() + static_cast<std::ptrdiff_t>(instanceIndex));
        MarkInstancesDirty();
        return true;
    }

    std::size_t FoliageComponent::GetTotalInstanceCount() const
    {
        std::size_t count = 0;
        for (const auto &type : m_types)
        {
            count += type.instances.size();
        }
        return count;
    }

    std::size_t FoliageComponent::GetSelectedTypeInstanceCount() const
    {
        const auto *type = GetSelectedType();
        return type ? type->instances.size() : 0;
    }

    const std::vector<FoliageCollisionCell> &FoliageComponent::BuildCollisionCells() const
    {
        const auto *owner = GetOwner();
        if (!owner)
        {
            m_cachedCollisionCells.clear();
            return m_cachedCollisionCells;
        }

        const glm::mat4 ownerTransform = owner->GetWorldTransform();
        if (m_collisionCacheRevision == m_revision && AreMatricesApproximatelyEqual(m_collisionCacheOwnerTransform, ownerTransform))
        {
            return m_cachedCollisionCells;
        }

        std::unordered_map<FoliageCellCoordinate, FoliageCollisionCell, FoliageCellCoordinateHash> cells;
        for (const auto &type : m_types)
        {
            if (!type.asset.collisionEnabled)
            {
                continue;
            }
            for (const auto &instance : type.instances)
            {
                const auto coordinate = GetCellCoordinate(type, instance.position);
                auto &cell = cells[coordinate];
                cell.coordinate = coordinate;
                // Capsule settings are authored in world units. The paint scale
                // often serves as a mesh import unit conversion (for example
                // 0.01 for centimetre-authored vegetation), so applying it to
                // the capsule makes otherwise correctly sized colliders tiny.
                const glm::mat4 transform = ComposeInstanceTransform(
                    ownerTransform, instance, glm::vec3(0.0f), false);
                cell.instances.push_back(FoliageCollisionInstance{
                    .instanceId = instance.id,
                    .worldTransform = transform,
                    .center = type.asset.collisionCenter,
                    .radius = type.asset.collisionRadius,
                    .height = type.asset.collisionHeight,
                });
            }
        }

        m_cachedCollisionCells.clear();
        m_cachedCollisionCells.reserve(cells.size());
        for (auto &[coordinate, cell] : cells)
        {
            (void)coordinate;
            m_cachedCollisionCells.push_back(std::move(cell));
        }
        m_collisionCacheRevision = m_revision;
        m_collisionCacheOwnerTransform = ownerTransform;
        return m_cachedCollisionCells;
    }

    void FoliageComponent::SetSelectedTypeIndex(int index)
    {
        EnsureTypeStorage();
        m_selectedTypeIndex = std::clamp(index, 0, static_cast<int>(m_types.size()) - 1);
    }

    FoliageType *FoliageComponent::GetType(std::size_t index)
    {
        return index < m_types.size() ? &m_types[index] : nullptr;
    }

    const FoliageType *FoliageComponent::GetType(std::size_t index) const
    {
        return index < m_types.size() ? &m_types[index] : nullptr;
    }

    FoliageType *FoliageComponent::GetSelectedType()
    {
        EnsureTypeStorage();
        return GetType(static_cast<std::size_t>(m_selectedTypeIndex));
    }

    const FoliageType *FoliageComponent::GetSelectedType() const
    {
        if (m_types.empty())
        {
            return nullptr;
        }
        const int index = std::clamp(m_selectedTypeIndex, 0, static_cast<int>(m_types.size()) - 1);
        return GetType(static_cast<std::size_t>(index));
    }

    FoliageType &FoliageComponent::AddType(std::string name)
    {
        if (name.empty())
        {
            name = DefaultTypeName(m_types.size());
        }
        m_types.push_back(FoliageType{.name = std::move(name)});
        m_selectedTypeIndex = static_cast<int>(m_types.size()) - 1;
        MarkRenderCommandsDirty();
        return m_types.back();
    }

    void FoliageComponent::RemoveType(std::size_t index)
    {
        if (index >= m_types.size())
        {
            return;
        }

        m_types.erase(m_types.begin() + static_cast<std::ptrdiff_t>(index));
        EnsureTypeStorage();
        m_selectedTypeIndex = std::clamp(m_selectedTypeIndex, 0, static_cast<int>(m_types.size()) - 1);
        MarkRenderCommandsDirty();
    }

    void FoliageComponent::SetTypeName(std::size_t index, const std::string &name)
    {
        if (auto *type = GetType(index))
        {
            type->name = name.empty() ? DefaultTypeName(index) : name;
            MarkRenderCommandsDirty();
        }
    }

    void FoliageComponent::SetTypeSubmeshIndex(std::size_t index, int submeshIndex)
    {
        if (auto *type = GetType(index))
        {
            if (!type->mesh || submeshIndex < 0)
            {
                type->submeshIndex = -1;
                type->submeshIndices.clear();
            }
            else
            {
                const int maxSubmeshIndex = static_cast<int>((std::max<std::size_t>)(type->mesh->GetSubmeshCount(), 1) - 1);
                type->submeshIndex = std::clamp(submeshIndex, 0, maxSubmeshIndex);
                type->submeshIndices = {type->submeshIndex};
            }
            MarkRenderCommandsDirty();
        }
    }

    void FoliageComponent::SetTypeSubmeshIndices(std::size_t index, const std::vector<int> &submeshIndices)
    {
        if (auto *type = GetType(index))
        {
            type->submeshIndices.clear();
            if (!type->mesh || submeshIndices.empty())
            {
                type->submeshIndex = -1;
                MarkRenderCommandsDirty();
                return;
            }

            const int maxSubmeshIndex = static_cast<int>((std::max<std::size_t>)(type->mesh->GetSubmeshCount(), 1) - 1);
            for (const int requestedIndex : submeshIndices)
            {
                if (requestedIndex < 0)
                {
                    continue;
                }
                const int clampedIndex = std::clamp(requestedIndex, 0, maxSubmeshIndex);
                if (std::find(type->submeshIndices.begin(), type->submeshIndices.end(), clampedIndex) == type->submeshIndices.end())
                {
                    type->submeshIndices.push_back(clampedIndex);
                }
            }

            type->submeshIndex = type->submeshIndices.size() == 1 ? type->submeshIndices.front() : -1;
            MarkRenderCommandsDirty();
        }
    }

    void FoliageComponent::SetTypeUseGeneratedLods(std::size_t index, bool useGeneratedLods)
    {
        if (auto *type = GetType(index))
        {
            if (type->useGeneratedLods == useGeneratedLods)
            {
                return;
            }

            type->useGeneratedLods = useGeneratedLods;
            RebuildTypeMeshFromReference(*type);
            RebuildTypeMaterialFromReference(*type);
            MarkRenderCommandsDirty();
        }
    }

    void FoliageComponent::SetTypeSourceMeshPath(std::size_t index, const std::string &sourceMeshPath)
    {
        if (auto *type = GetType(index))
        {
            type->sourceMeshPath = sourceMeshPath;
            RebuildTypeMeshFromReference(*type);
            RebuildTypeMaterialFromReference(*type);
            MarkRenderCommandsDirty();
        }
    }

    void FoliageComponent::SetTypeMaterialAssetReference(std::size_t index, const std::string &materialAssetReference)
    {
        if (auto *type = GetType(index))
        {
            type->materialAssetReference = materialAssetReference;
            RebuildTypeMaterialFromReference(*type);
            MarkRenderCommandsDirty();
        }
    }

    void FoliageComponent::ClearTypeMaterialAssetReference(std::size_t index)
    {
        if (auto *type = GetType(index))
        {
            type->materialAssetReference.clear();
            type->materialOverride = nullptr;
            MarkRenderCommandsDirty();
        }
    }

    bool FoliageComponent::SetTypeAssetReference(std::size_t index, const std::string &assetReference)
    {
        auto *type = GetType(index);
        if (!type) return false;
        if (assetReference.empty())
        {
            type->asset.assetReference.clear();
            MarkInstancesDirty();
            return true;
        }
        FoliageTypeAsset loaded;
        loaded.assetReference = assetReference;
        if (!LoadFoliageTypeAsset(core::Engine::GetInstance().GetAssetManager().ResolveAssetPath(assetReference), loaded))
            return false;
        type->asset = std::move(loaded);
        MarkInstancesDirty();
        return true;
    }

    void FoliageComponent::SetTypeCollisionEnabled(std::size_t index, bool enabled)
    {
        if (auto *type = GetType(index); type && type->asset.collisionEnabled != enabled)
        {
            type->asset.collisionEnabled = enabled;
            MarkInstancesDirty();
        }
    }

    void FoliageComponent::SetTypeCollisionCapsule(std::size_t index, const glm::vec3 &center, float radius, float height)
    {
        if (auto *type = GetType(index))
        {
            type->asset.collisionCenter = center;
            type->asset.collisionRadius = (std::max)(radius, 0.01f);
            type->asset.collisionHeight = (std::max)(height, type->asset.collisionRadius * 2.0f);
            MarkInstancesDirty();
        }
    }

    void FoliageComponent::SetTypeCellSize(std::size_t index, float cellSize)
    {
        if (auto *type = GetType(index))
        {
            type->asset.cellSize = (std::max)(cellSize, 1.0f);
            MarkInstancesDirty();
        }
    }

    void FoliageComponent::SetTypeMaxDrawDistance(std::size_t index, float distance)
    {
        if (auto *type = GetType(index))
        {
            type->asset.maxDrawDistance = (std::max)(distance, 0.0f);
            MarkRenderCommandsDirty();
        }
    }

    void FoliageComponent::SetTypeMeshAndMaterials(std::size_t index,
                                                   render::Mesh *mesh,
                                                   const std::vector<render::Material *> &materials,
                                                   const std::string &sourceMeshPath)
    {
        if (auto *type = GetType(index))
        {
            type->mesh = mesh;
            AssignFoliageMaterials(*type, materials);
            type->sourceMeshPath = sourceMeshPath;
            SanitizeTypeSubmeshSelection(*type);
            if (type->materialAssetReference.empty())
            {
                type->materialOverride = nullptr;
            }
            else
            {
                RebuildTypeMaterialFromReference(*type);
            }
            MarkRenderCommandsDirty();
        }
    }

    FoliageType &FoliageComponent::EnsureSelectedType()
    {
        EnsureTypeStorage();
        m_selectedTypeIndex = std::clamp(m_selectedTypeIndex, 0, static_cast<int>(m_types.size()) - 1);
        return m_types[static_cast<std::size_t>(m_selectedTypeIndex)];
    }

    void FoliageComponent::EnsureTypeStorage()
    {
        if (m_types.empty())
        {
            m_types.push_back(FoliageType{.name = "Foliage 1"});
            m_selectedTypeIndex = 0;
            MarkRenderCommandsDirty();
        }
    }

    void FoliageComponent::MarkRenderCommandsDirty()
    {
        m_renderCommandCacheDirty = true;
        m_hasCachedRenderCommandModel = false;
        m_cachedRenderCommands.clear();
        if (auto *owner = GetOwner())
        {
            if (auto *scene = owner->GetScene())
            {
                scene->MarkShadowLightsDirty();
            }
        }
    }

    void FoliageComponent::MarkInstancesDirty()
    {
        ++m_revision;
        MarkRenderCommandsDirty();
        if (auto *owner = GetOwner())
        {
            if (auto *scene = owner->GetScene())
            {
                scene->InvalidateFoliagePhysics();
            }
        }
    }

    void FoliageComponent::EnsureStableInstanceIds()
    {
        std::unordered_set<std::uint64_t> ids;
        m_nextInstanceId = 1;
        for (auto &type : m_types)
        {
            for (auto &instance : type.instances)
            {
                if (instance.id == 0 || !ids.insert(instance.id).second)
                {
                    while (ids.contains(m_nextInstanceId)) ++m_nextInstanceId;
                    instance.id = m_nextInstanceId++;
                    ids.insert(instance.id);
                }
                m_nextInstanceId = (std::max)(m_nextInstanceId, instance.id + 1);
            }
        }
    }

    FoliageCellCoordinate FoliageComponent::GetCellCoordinate(const FoliageType &type, const glm::vec3 &position) const
    {
        const float cellSize = (std::max)(type.asset.cellSize, 1.0f);
        return {
            static_cast<int>(std::floor(position.x / cellSize)),
            static_cast<int>(std::floor(position.z / cellSize)),
        };
    }

    render::Material *FoliageComponent::GetMaterialForTypeSubmesh(const FoliageType &type, std::size_t submeshIndex) const
    {
        if (type.materialOverride)
        {
            return type.materialOverride;
        }

        if (type.mesh && submeshIndex < type.mesh->GetSubmeshCount())
        {
            const auto materialIndex = static_cast<std::size_t>(type.mesh->GetSubmesh(submeshIndex).materialIndex);
            if (materialIndex < type.materials.size() && type.materials[materialIndex])
            {
                return type.materials[materialIndex];
            }
        }

        if (!type.materials.empty() && type.materials.front())
        {
            return type.materials.front();
        }

        return m_material;
    }

    void FoliageComponent::RebuildTypeMaterialFromReference(FoliageType &type)
    {
        if (type.materialAssetReference.empty())
        {
            type.materialOverride = nullptr;
            if (!m_material)
            {
                m_material = core::Engine::GetInstance().GetAssetManager().LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference));
            }
            return;
        }

        type.materialOverride = core::Engine::GetInstance().GetAssetManager().LoadMaterialAsset(type.materialAssetReference);
    }

    void FoliageComponent::RebuildTypeMeshFromReference(FoliageType &type)
    {
        type.mesh = nullptr;
        type.materials.clear();
        type.ownedMaterials.clear();
        if (type.sourceMeshPath.empty())
        {
            return;
        }

        auto &engine = core::Engine::GetInstance();
        if (auto *mesh = engine.GetAssetManager().LoadMeshAsset(type.sourceMeshPath))
        {
            type.mesh = mesh;
            const auto &materialReferences = engine.GetAssetManager().GetMeshAssetMaterialReferences(type.sourceMeshPath);
            std::vector<render::Material *> loadedMaterials;
            loadedMaterials.reserve((std::max<std::size_t>)(materialReferences.size(), 1));
            for (const auto &materialReference : materialReferences)
            {
                loadedMaterials.push_back(engine.GetAssetManager().LoadMaterialAsset(materialReference));
            }
            if (loadedMaterials.empty())
            {
                loadedMaterials.push_back(engine.GetAssetManager().LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference)));
            }
            AssignFoliageMaterials(type, loadedMaterials);
            SanitizeTypeSubmeshSelection(type);
            return;
        }

        const std::string resolvedPath = engine.GetAssetManager().ResolveMeshAssetSourcePath(type.sourceMeshPath);
        auto imported = type.useGeneratedLods ? engine.GenerateMeshAssetLods(resolvedPath) : engine.ImportMeshAsset(resolvedPath);
        type.mesh = imported.mesh;
        AssignFoliageMaterials(type, imported.materials);
        SanitizeTypeSubmeshSelection(type);
    }
}
