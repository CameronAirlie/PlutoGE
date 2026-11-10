#include "PlutoGE/render/RenderObjectIdentity.h"
#include "PlutoGE/scene/components/FoliageComponent.h"

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/AssetMetadata.h"
#include "PlutoGE/assets/ModelImportSettings.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/RenderSceneFrontend.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/FastTextNumbers.h"
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

        // The import options a source model's sidecar asks for (Content Browser import settings: vegetation LODs,
        // triangle budget), or the importer's defaults when it has none. Foliage that references a model directly
        // imports it here, so without this it would ignore those settings and draw every instance at full detail.
        assetimport::MeshImportOptions SourceModelImportOptions(const std::string &sourcePath)
        {
            assets::AssetMetadata metadata;
            assets::ModelImportSettings settings;
            if (assets::LoadAssetMetadata(assets::GetAssetMetadataPath(sourcePath), metadata) ==
                    assets::AssetMetadataStatus::Success &&
                assets::ReadModelImportSettings(metadata, settings) == assets::ModelImportSettingsStatus::Success)
                return settings.meshOptions;
            return {};
        }

        // Wind values tolerate malformed text by keeping the current value.
        float ParseFloatOr(const std::string &text, float fallback)
        {
            try { return std::stof(text); } catch (...) { return fallback; }
        }

        std::string SerializeInstances(const std::vector<FoliageInstance> &instances)
        {
            constexpr std::size_t kTypicalRecordBytes = 90;
            std::string output;
            output.reserve(instances.size() * kTypicalRecordBytes);
            for (const auto &instance : instances)
            {
                if (!output.empty()) output.push_back(';');
                AppendNumber(output, instance.id);
                const float values[9]{instance.position.x, instance.position.y, instance.position.z,
                                      instance.rotationDegrees.x, instance.rotationDegrees.y, instance.rotationDegrees.z,
                                      instance.scale.x, instance.scale.y, instance.scale.z};
                for (const float value : values)
                {
                    output.push_back(',');
                    AppendNumber(output, value);
                }
            }
            return output;
        }

        std::vector<FoliageInstance> DeserializeInstances(const std::string &value)
        {
            std::vector<FoliageInstance> instances;
            instances.reserve(static_cast<std::size_t>(std::count(value.begin(), value.end(), ';')) + 1);
            std::string_view remaining = value;
            std::string_view record;
            while (!remaining.empty() && NextToken(remaining, ';', record))
            {
                // Records written before instance IDs have nine fields; unparsable ones are skipped.
                std::string_view token;
                std::uint64_t instanceId = 0;
                if (std::count(record.begin(), record.end(), ',') == 9 && NextToken(record, ',', token))
                    instanceId = ParseNumberToken<std::uint64_t>(token).value_or(0);
                float values[9]{};
                int valueIndex = 0;
                while (valueIndex < 9 && NextToken(record, ',', token))
                {
                    const auto parsed = ParseNumberToken<float>(token);
                    if (!parsed)
                    {
                        valueIndex = 0;
                        break;
                    }
                    values[valueIndex++] = *parsed;
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
            if (!type.asset.submeshIndices.empty())
            {
                selectedSubmeshes.reserve(type.asset.submeshIndices.size());
                for (const int submeshIndex : type.asset.submeshIndices)
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
            else if (type.asset.submeshIndex >= 0 && static_cast<std::size_t>(type.asset.submeshIndex) < submeshCount)
            {
                selectedSubmeshes.push_back(static_cast<std::size_t>(type.asset.submeshIndex));
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
                type.asset.submeshIndex = -1;
                type.asset.submeshIndices.clear();
                return;
            }

            const int maxSubmeshIndex = static_cast<int>((std::max<std::size_t>)(type.mesh->GetSubmeshCount(), 1) - 1);
            if (type.asset.submeshIndex > maxSubmeshIndex)
            {
                type.asset.submeshIndex = -1;
            }

            std::vector<int> sanitizedIndices;
            sanitizedIndices.reserve(type.asset.submeshIndices.size());
            for (const int submeshIndex : type.asset.submeshIndices)
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

            type.asset.submeshIndices = std::move(sanitizedIndices);
            if (!type.asset.submeshIndices.empty())
            {
                type.asset.submeshIndex = type.asset.submeshIndices.size() == 1 ? type.asset.submeshIndices.front() : -1;
            }
        }

        render::MaterialConfig FoliageMaterialConfig(render::MaterialConfig config)
        {
            if (config.alphaMode == render::AlphaMode::Blend && config.albedoTexture)
            {
                config.alphaMode = render::AlphaMode::Mask;
                config.alphaCutoff = (std::max)(config.alphaCutoff, 0.35f);
            }
            return config;
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

                type.ownedMaterials.push_back(std::make_unique<render::Material>(FoliageMaterialConfig(*materialConfig)));
                type.materials.push_back(type.ownedMaterials.back().get());
            }
        }

        // Moller-Trumbore, two-sided: foliage is routinely single-sheet geometry seen from behind.
        std::optional<float> IntersectRayTriangle(const glm::vec3 &origin, const glm::vec3 &direction,
                                                  const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c)
        {
            const glm::vec3 edge1 = b - a;
            const glm::vec3 edge2 = c - a;
            const glm::vec3 p = glm::cross(direction, edge2);
            const float determinant = glm::dot(edge1, p);
            if (std::abs(determinant) < 1e-12f) return std::nullopt;
            const float inverse = 1.0f / determinant;
            const glm::vec3 s = origin - a;
            const float u = glm::dot(s, p) * inverse;
            if (u < 0.0f || u > 1.0f) return std::nullopt;
            const glm::vec3 q = glm::cross(s, edge1);
            const float v = glm::dot(direction, q) * inverse;
            if (v < 0.0f || u + v > 1.0f) return std::nullopt;
            const float t = glm::dot(edge2, q) * inverse;
            return t >= 0.0f ? std::optional<float>(t) : std::nullopt;
        }

        std::string DefaultTypeName(std::size_t index)
        {
            return "Foliage " + std::to_string(index + 1);
        }

        // FNV-1a over everything that shapes a cell's capsules, in order.
        std::uint64_t ComputeCollisionCellSignature(const FoliageCollisionCell &cell)
        {
            std::uint64_t hash = 14695981039346656037ull;
            const auto mix = [&hash](const void *data, std::size_t size)
            {
                const auto *bytes = static_cast<const unsigned char *>(data);
                for (std::size_t index = 0; index < size; ++index)
                {
                    hash ^= bytes[index];
                    hash *= 1099511628211ull;
                }
            };
            mix(&cell.coordinate.x, sizeof(cell.coordinate.x));
            mix(&cell.coordinate.z, sizeof(cell.coordinate.z));
            for (const auto &instance : cell.instances)
            {
                mix(&instance.instanceId, sizeof(instance.instanceId));
                mix(&instance.worldTransform, sizeof(instance.worldTransform));
                mix(&instance.center, sizeof(instance.center));
                mix(&instance.radius, sizeof(instance.radius));
                mix(&instance.height, sizeof(instance.height));
            }
            // Never 0, so a default-constructed record never matches a real cell.
            return hash == 0 ? 1 : hash;
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

        // The mesh asset's baked impostor with its atlas textures loaded; null when it has none or they fail to load.
        std::shared_ptr<const render::MeshImpostor> LoadImpostor(assets::AssetManager &assets, const std::string &meshReference)
        {
            const auto &impostor = assets.GetMeshAssetMetadata(meshReference).impostor;
            if (!impostor) return nullptr;
            const auto *albedo = assets.LoadTexture(assets.ResolveAssetPath(impostor->albedoReference).c_str(),
                                                    render::TextureColorSpace::SRGB);
            const auto *normalDepth = assets.LoadTexture(assets.ResolveAssetPath(impostor->normalDepthReference).c_str(),
                                                         render::TextureColorSpace::Linear);
            if (!albedo || !normalDepth) return nullptr;
            return std::make_shared<const render::MeshImpostor>(render::MeshImpostor{impostor->parameters, albedo, normalDepth});
        }

        bool CompareRenderCommandKeys(const render::RenderCommand &a, const render::RenderCommand &b)
        {
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

    const std::vector<render::RenderCommand> &FoliageComponent::GetRenderCommands()
    {
        if (auto *owner = GetOwner())
        {
            const glm::mat4 ownerTransform = owner->GetWorldTransform();
            if (m_renderCommandCacheDirty ||
                !m_hasCachedRenderCommandModel ||
                !AreMatricesApproximatelyEqual(m_cachedRenderCommandModel, ownerTransform))
            {
                RebuildRenderCommandCache(ownerTransform);
            }
        }
        return m_cachedRenderCommands;
    }

    void FoliageComponent::SubmitRenderCommands()
    {
        if (!GetOwner())
        {
            return;
        }
        const auto &commands = GetRenderCommands();
        if (commands.empty())
        {
            return;
        }

        core::Engine::GetInstance().GetRenderScene().SubmitSortedRenderCommands(commands, true);
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
            // One submesh's commands draw the impostor cards: the first alpha-masked one (the leaves, whose material
            // the cards shade with), else the first.
            std::size_t impostorOwner = selectedSubmeshes.front();
            for (const std::size_t submeshIndex : selectedSubmeshes)
                if (const auto *material = GetMaterialForTypeSubmesh(type, submeshIndex);
                    material && material->ReadConfig().alphaMode == render::AlphaMode::Mask)
                {
                    impostorOwner = submeshIndex;
                    break;
                }

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
                // Swaying instances reach beyond their rest bounds; grow the cluster so culling stays conservative.
                if (const float sway = render::FoliageWindMaxDisplacement(type.asset.wind); sway > 0.0f)
                {
                    float upScale = 0.0f;
                    for (const auto &model : *cluster.models)
                        upScale = (std::max)(upScale, glm::length(glm::vec3(model[1])));
                    cluster.bounds.radius += sway * upScale;
                }
                for (const std::size_t submeshIndex : selectedSubmeshes)
                {
                    auto *material = GetMaterialForTypeSubmesh(type, submeshIndex);
                    if (!material)
                    {
                        continue;
                    }

                    render::RenderCommand command;
                    command.ownerEntity = GetOwner() ? GetOwner()->GetID() : 0;
                    command.model = cluster.models->front();
                    command.previousModel = command.model;
                    command.material = material;
                    command.mesh = type.mesh;
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
                    command.lodDistanceScale = type.asset.lodDistanceScale;
                    command.impostor = type.impostor.get();
                    command.impostorOwner = type.impostor && submeshIndex == impostorOwner;
                    command.isStatic = m_isStatic;
                    command.castsShadow = m_castShadows;
                    command.foliageWind = type.asset.wind;
                    // The instance array is rebuilt (a new identity) whenever it changes, so the renderer may reuse
                    // its prepared packet, batches and shadow signatures for as long as this command lives.
                    command.sourceObject = render::RenderObjectIdentity().Value();
                    command.sourceRevision = 1;
                    m_cachedRenderCommands.push_back(command);
                }
            }
        }

        std::sort(m_cachedRenderCommands.begin(), m_cachedRenderCommands.end(), CompareRenderCommandKeys);
        m_cachedRenderCommandModel = ownerTransform;
        m_hasCachedRenderCommandModel = true;
        m_renderCommandCacheDirty = false;
    }

    bool FoliageComponent::IsRetiredProperty(std::string_view name) const
    {
        return name == "PaintEnabled" || name == "BrushMode" || name == "BrushRadius" || name == "Density" ||
               name == "MinScale" || name == "MaxScale" ||
               // Painting used to record this override, though only per-type instance lists exist.
               name == "Instances";
    }

    FoliageInstance ApplyFoliageInstanceWorldEdit(const glm::mat4 &ownerTransform, const FoliageInstance &instance,
                                                  const FoliageInstanceWorldEdit &edit)
    {
        FoliageInstance result = instance;
        const glm::vec3 worldPosition = glm::vec3(ownerTransform * glm::vec4(instance.position, 1.0f));
        const glm::vec3 moved = edit.pivot + edit.rotation * ((worldPosition - edit.pivot) * edit.scale) + edit.translation;
        result.position = glm::vec3(glm::inverse(ownerTransform) * glm::vec4(moved, 1.0f));
        result.scale = glm::max(instance.scale * edit.scale, glm::vec3(0.0001f));
        if (edit.rotation != glm::mat3(1.0f))
        {
            // Instances draw with the owner's unscaled axes, then yaw, pitch and roll (ComposeInstanceTransform).
            const glm::mat3 ownerAxes(SafeNormalizedAxis(glm::vec3(ownerTransform[0]), glm::vec3(1, 0, 0)),
                                      SafeNormalizedAxis(glm::vec3(ownerTransform[1]), glm::vec3(0, 1, 0)),
                                      SafeNormalizedAxis(glm::vec3(ownerTransform[2]), glm::vec3(0, 0, 1)));
            glm::mat4 local(1.0f);
            local = glm::rotate(local, glm::radians(instance.rotationDegrees.y), glm::vec3(0, 1, 0));
            local = glm::rotate(local, glm::radians(instance.rotationDegrees.x), glm::vec3(1, 0, 0));
            local = glm::rotate(local, glm::radians(instance.rotationDegrees.z), glm::vec3(0, 0, 1));
            const glm::mat3 r = glm::inverse(ownerAxes) * edit.rotation * ownerAxes * glm::mat3(local);
            // R = Ry * Rx * Rz: column 2 row 1 is -sin(pitch).
            result.rotationDegrees = glm::degrees(glm::vec3(std::asin(glm::clamp(-r[2][1], -1.0f, 1.0f)),
                                                            std::atan2(r[2][0], r[2][2]),
                                                            std::atan2(r[0][1], r[1][1])));
        }
        return result;
    }

    std::vector<std::string> RemapFoliageOverridesAfterTypeRemoval(const std::vector<std::string> &overrides, std::size_t removedIndex)
    {
        constexpr std::string_view prefix = "Component:FoliageComponent:Type.";
        std::vector<std::string> remapped;
        remapped.reserve(overrides.size());
        for (const auto &path : overrides)
        {
            const auto separator = path.rfind(prefix, 0) == 0 ? path.find('.', prefix.size()) : std::string::npos;
            std::size_t index = 0;
            bool indexed = separator != std::string::npos && separator > prefix.size();
            for (std::size_t at = prefix.size(); indexed && at < separator; ++at)
            {
                indexed = path[at] >= '0' && path[at] <= '9';
                index = index * 10 + static_cast<std::size_t>(path[at] - '0');
            }
            if (!indexed)
                remapped.push_back(path);
            else if (index > removedIndex)
                remapped.push_back(std::string(prefix) + std::to_string(index - 1) + path.substr(separator));
            else if (index < removedIndex)
                remapped.push_back(path);
        }
        return remapped;
    }

    std::vector<Property> FoliageComponent::Serialize() const
    {
        return MaterializeSharedProperties(SerializeProperties(HeavyDetail::Shared));
    }

    std::vector<Property> FoliageComponent::SerializeForInspector() const
    {
        return SerializeProperties(HeavyDetail::Omit);
    }

    std::vector<Property> FoliageComponent::SerializeShared() const
    {
        return SerializeProperties(HeavyDetail::Shared);
    }

    std::vector<Property> FoliageComponent::SerializeProperties(HeavyDetail detail) const
    {
        std::vector<Property> properties = {
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
            properties.push_back({prefix + "SourceMesh", PropertyType::String, type.asset.sourceMeshPath});
            properties.push_back({prefix + "ModelAssetId", PropertyType::String, type.asset.modelAssetId});
            properties.push_back({prefix + "ModelObjectId", PropertyType::String, std::to_string(type.asset.modelObjectId)});
            properties.push_back({prefix + "MaterialAsset", PropertyType::String, type.asset.materialAssetReference});
            properties.push_back({prefix + "SubmeshIndex", PropertyType::Int, std::to_string(type.asset.submeshIndex)});
            properties.push_back({prefix + "SubmeshIndices", PropertyType::String, SerializeSubmeshIndices(type.asset.submeshIndices)});
            properties.push_back({prefix + "UseGeneratedLods", PropertyType::Bool, type.asset.useGeneratedLods ? "true" : "false"});
            properties.push_back({prefix + "AlignToTerrainNormal", PropertyType::Bool, type.asset.alignToTerrainNormal ? "true" : "false"});
            properties.push_back({prefix + "MinScale", PropertyType::Float, std::to_string(type.asset.minScale)});
            properties.push_back({prefix + "MaxScale", PropertyType::Float, std::to_string(type.asset.maxScale)});
            properties.push_back({prefix + "AssetReference", PropertyType::String, type.asset.assetReference});
            properties.push_back({prefix + "CellSize", PropertyType::Float, std::to_string(type.asset.cellSize)});
            properties.push_back({prefix + "MaxDrawDistance", PropertyType::Float, std::to_string(type.asset.maxDrawDistance)});
            properties.push_back({prefix + "LodDistanceScale", PropertyType::Float, std::to_string(type.asset.lodDistanceScale)});
            properties.push_back({prefix + "CollisionEnabled", PropertyType::Bool, type.asset.collisionEnabled ? "true" : "false"});
            properties.push_back({prefix + "CollisionCenter", PropertyType::Vec3,
                                  std::to_string(type.asset.collisionCenter.x) + "," +
                                  std::to_string(type.asset.collisionCenter.y) + "," +
                                  std::to_string(type.asset.collisionCenter.z)});
            properties.push_back({prefix + "CollisionRadius", PropertyType::Float, std::to_string(type.asset.collisionRadius)});
            properties.push_back({prefix + "CollisionHeight", PropertyType::Float, std::to_string(type.asset.collisionHeight)});
            properties.push_back({prefix + "WindEnabled", PropertyType::Bool, type.asset.wind.enabled ? "true" : "false"});
            properties.push_back({prefix + "WindStiffness", PropertyType::Float, std::to_string(type.asset.wind.stiffness)});
            properties.push_back({prefix + "WindFlutter", PropertyType::Float, std::to_string(type.asset.wind.flutter)});
            properties.push_back({prefix + "WindBendHeight", PropertyType::Float, std::to_string(type.asset.wind.bendHeight)});
            properties.push_back({prefix + "WindPivotHeight", PropertyType::Float, std::to_string(type.asset.wind.pivotHeight)});
            properties.push_back({prefix + "ProxyPrefab", PropertyType::String, type.asset.proxyPrefabReference});
            if (detail == HeavyDetail::Shared)
            {
                Property instances{prefix + "Instances", PropertyType::String};
                instances.sharedValue = type.instancesText.Get(m_instanceTextRevision, [&type] { return SerializeInstances(type.instances); });
                properties.push_back(std::move(instances));
            }
        }

        return properties;
    }

    void FoliageComponent::Deserialize(const std::vector<Property> &properties)
    {
        DeserializeProperties(properties, MissingInstances::Clear);
    }

    void FoliageComponent::DeserializeKeepingHeavyData(const std::vector<Property> &properties)
    {
        DeserializeProperties(properties, MissingInstances::Keep);
    }

    void FoliageComponent::DeserializeProperties(const std::vector<Property> &properties, MissingInstances missingInstances)
    {
        std::unordered_map<std::size_t, FoliageType> parsedTypes;
        std::unordered_set<std::size_t> typesWithInstances;
        std::string legacySourceMeshPath;
        std::string legacyMaterialAssetReference;
        std::string legacyInstances;
        int typeCount = 0;
        // Brush scale used to be one range per component; types written then start with it.
        std::optional<float> legacyMinScale;
        std::optional<float> legacyMaxScale;
        std::unordered_set<std::size_t> typesWithScaleRange;

        for (const auto &property : properties)
        {
            // Retired brush records (PaintEnabled, BrushMode, BrushRadius, Density) are ignored: see IsRetiredProperty.
            if (property.name == "MinScale")
                legacyMinScale = ParseFloatOr(property.value, 0.8f);
            else if (property.name == "MaxScale")
                legacyMaxScale = ParseFloatOr(property.value, 1.2f);
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
                    type.asset.sourceMeshPath = property.value;
                else if (fieldName == "ModelAssetId")
                    type.asset.modelAssetId = property.value;
                else if (fieldName == "ModelObjectId")
                {
                    try { type.asset.modelObjectId = std::stoull(property.value); } catch (...) { type.asset.modelObjectId = 0; }
                }
                else if (fieldName == "MaterialAsset")
                    type.asset.materialAssetReference = property.value;
                else if (fieldName == "SubmeshIndex")
                    type.asset.submeshIndex = std::stoi(property.value);
                else if (fieldName == "SubmeshIndices")
                    type.asset.submeshIndices = DeserializeSubmeshIndices(property.value);
                else if (fieldName == "UseGeneratedLods")
                    type.asset.useGeneratedLods = property.value == "true" || property.value == "1";
                else if (fieldName == "AlignToTerrainNormal")
                    type.asset.alignToTerrainNormal = property.value == "true" || property.value == "1";
                else if (fieldName == "MinScale")
                {
                    type.asset.minScale = ParseFloatOr(property.value, type.asset.minScale);
                    typesWithScaleRange.insert(typeIndex);
                }
                else if (fieldName == "MaxScale")
                {
                    type.asset.maxScale = ParseFloatOr(property.value, type.asset.maxScale);
                    typesWithScaleRange.insert(typeIndex);
                }
                else if (fieldName == "AssetReference")
                    type.asset.assetReference = property.value;
                else if (fieldName == "CellSize")
                    type.asset.cellSize = (std::max)(1.0f, std::stof(property.value));
                else if (fieldName == "MaxDrawDistance")
                    type.asset.maxDrawDistance = (std::max)(0.0f, std::stof(property.value));
                else if (fieldName == "LodDistanceScale")
                    type.asset.lodDistanceScale = render::SanitizeLodDistanceScale(
                        ParseFloatOr(property.value, render::DefaultLodDistanceScale));
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
                else if (fieldName == "WindEnabled")
                    type.asset.wind.enabled = property.value == "true" || property.value == "1";
                else if (fieldName == "WindStiffness")
                    type.asset.wind.stiffness = ParseFloatOr(property.value, type.asset.wind.stiffness);
                else if (fieldName == "WindFlutter")
                    type.asset.wind.flutter = ParseFloatOr(property.value, type.asset.wind.flutter);
                else if (fieldName == "WindBendHeight")
                    type.asset.wind.bendHeight = ParseFloatOr(property.value, type.asset.wind.bendHeight);
                else if (fieldName == "WindPivotHeight")
                    type.asset.wind.pivotHeight = ParseFloatOr(property.value, type.asset.wind.pivotHeight);
                else if (fieldName == "ProxyPrefab")
                    type.asset.proxyPrefabReference = property.value;
                else if (fieldName == "Instances")
                {
                    type.instances = DeserializeInstances(property.Text());
                    typesWithInstances.insert(typeIndex);
                }
            }
        }

        // Types are rebuilt below, so anything to keep has to move across before they are cleared.
        // Nothing but kept instances means their text is still valid, so a property edit does not re-encode them.
        const auto previousTextRevision = m_instanceTextRevision;
        const bool instancesUnchanged = missingInstances == MissingInstances::Keep && typesWithInstances.empty() &&
                                        parsedTypes.size() == m_types.size();
        if (missingInstances == MissingInstances::Keep)
            for (auto &[typeIndex, type] : parsedTypes)
                if (typeIndex < m_types.size() && !typesWithInstances.contains(typeIndex))
                {
                    type.instances = std::move(m_types[typeIndex].instances);
                    type.instancesText = std::move(m_types[typeIndex].instancesText);
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
            legacyType.asset.sourceMeshPath = legacySourceMeshPath;
            legacyType.asset.materialAssetReference = legacyMaterialAssetReference;
            legacyType.instances = DeserializeInstances(legacyInstances);
            m_types.push_back(std::move(legacyType));
        }

        for (std::size_t index = 0; index < m_types.size(); ++index)
        {
            if (typesWithScaleRange.contains(index))
                continue;
            if (legacyMinScale) m_types[index].asset.minScale = *legacyMinScale;
            if (legacyMaxScale) m_types[index].asset.maxScale = *legacyMaxScale;
        }

        EnsureTypeStorage();
        m_selectedTypeIndex = std::clamp(m_selectedTypeIndex, 0, static_cast<int>(m_types.size()) - 1);
        for (auto &type : m_types)
        {
            // The linked file is authoritative; if it cannot be read the scene's copy keeps the type working, and the
            // inspector reports why.
            type.assetLoadError.clear();
            if (!type.asset.assetReference.empty())
            {
                LoadFoliageTypeAsset(core::Engine::GetInstance().GetAssetManager().ResolveAssetPath(type.asset.assetReference),
                                     type.asset, &type.assetLoadError);
            }
            SanitizeFoliageTypeAsset(type.asset);
            if (type.asset.modelAssetId.empty() && !type.asset.sourceMeshPath.empty())
            {
                // Upgrade legacy foliage that stored only an Imported/... mesh
                // reference. Its mesh metadata already carries the stable model
                // identity needed to follow the canonical artifact after reimport.
                const auto &metadata = core::Engine::GetInstance().GetAssetManager().GetMeshAssetMetadata(type.asset.sourceMeshPath);
                type.asset.modelAssetId = metadata.sourceAssetId;
                type.asset.modelObjectId = metadata.sourceObjectId;
            }
            if (!type.asset.modelAssetId.empty() && type.asset.modelObjectId != 0)
            {
                const auto resolved = core::Engine::GetInstance().GetAssetManager().ResolveModelObject(
                    type.asset.modelAssetId, type.asset.modelObjectId);
                if (!resolved.empty()) type.asset.sourceMeshPath = resolved;
            }
            RebuildTypeMeshFromReference(type);
            RebuildTypeMaterialFromReference(type);
        }
        EnsureStableInstanceIds();
        // Deserialization is also used when restoring editor state. Advance the
        // instance revision so an existing Bullet cache cannot keep the old (or
        // empty) collider set for the newly restored placements.
        MarkInstancesDirty();
        if (instancesUnchanged) m_instanceTextRevision = previousTextRevision;
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
        return type ? type->asset.sourceMeshPath : empty;
    }

    void FoliageComponent::SetMaterialAssetReference(const std::string &materialAssetReference)
    {
        SetTypeMaterialAssetReference(static_cast<std::size_t>(m_selectedTypeIndex), materialAssetReference);
    }

    const std::string &FoliageComponent::GetMaterialAssetReference() const
    {
        static const std::string empty;
        const auto *type = GetSelectedType();
        return type ? type->asset.materialAssetReference : empty;
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

    bool FoliageComponent::ApplyBrush(const FoliageBrush &brush, const glm::vec3 &worldPosition,
                                      const FoliageSurfaceSampler &sampleSurface)
    {
        const float radius = (std::max)(brush.radius, 0.05f);
        if (brush.mode == FoliageBrushMode::Remove)
            return RemoveInstancesAtWorldPosition(worldPosition, radius);
        return PaintAtWorldPosition(FoliageBrush{.mode = brush.mode, .radius = radius, .density = std::clamp(brush.density, 1, 100)},
                                    worldPosition, sampleSurface);
    }

    bool FoliageComponent::PaintAtWorldPosition(const FoliageBrush &brush, const glm::vec3 &worldPosition,
                                                const FoliageSurfaceSampler &sampleSurface)
    {
        auto *owner = GetOwner();
        auto &type = EnsureSelectedType();
        if (!owner || !type.mesh || !sampleSurface)
        {
            return false;
        }

        const glm::mat4 inverseWorld = glm::inverse(owner->GetWorldTransform());
        const glm::vec3 localCenter = glm::vec3(inverseWorld * glm::vec4(worldPosition, 1.0f));
        if (!std::isfinite(localCenter.x) || !std::isfinite(localCenter.y) || !std::isfinite(localCenter.z))
            return false;
        const auto coordinateSeed = [](float value)
        {
            // Keep quantization deterministic without overflowing lround for
            // distant worlds. Conversion to uint32 is always in range.
            constexpr double modulus = 4294967296.0;
            double wrapped = std::fmod(std::round(static_cast<double>(value) * 100.0), modulus);
            if (wrapped < 0) wrapped += modulus;
            return static_cast<std::uint32_t>(wrapped);
        };
        std::seed_seq seed{
            coordinateSeed(localCenter.x), coordinateSeed(localCenter.z),
            static_cast<std::uint32_t>(type.instances.size()),
            static_cast<std::uint32_t>(m_selectedTypeIndex),
        };
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> angleDistribution(0.0f, glm::two_pi<float>());
        std::uniform_real_distribution<float> radiusDistribution(0.0f, 1.0f);
        std::uniform_real_distribution<float> yawDistribution(0.0f, 360.0f);
        std::uniform_real_distribution<float> scaleDistribution(type.asset.minScale, type.asset.maxScale);

        const auto startCount = type.instances.size();
        type.instances.reserve(type.instances.size() + static_cast<std::size_t>(brush.density));
        for (int index = 0; index < brush.density; ++index)
        {
            const float angle = angleDistribution(rng);
            const float radius = std::sqrt(radiusDistribution(rng)) * brush.radius;
            glm::vec3 localPosition = localCenter + glm::vec3(std::cos(angle) * radius, 0.0f, std::sin(angle) * radius);
            const auto surface = sampleSurface(localPosition.x, localPosition.z);
            const float scale = scaleDistribution(rng);
            const float yaw = yawDistribution(rng);
            if (!surface || !std::isfinite(surface->height) ||
                !std::isfinite(surface->normal.x) || !std::isfinite(surface->normal.y) ||
                !std::isfinite(surface->normal.z))
                continue;
            localPosition.y = surface->height;
            const float largestNormal = std::max({std::abs(surface->normal.x), std::abs(surface->normal.y), std::abs(surface->normal.z)});
            if (largestNormal < 0.0001f) continue;
            const glm::vec3 localSurfaceNormal = glm::normalize(surface->normal / largestNormal);
            // Normals use inverse transpose; instance axes use the ordinary
            // owner transform. Convert the desired world axis back to local
            // space so rotated/nonuniformly scaled terrain does not lean trees.
            const glm::vec3 worldUp = type.asset.alignToTerrainNormal
                ? glm::transpose(glm::mat3(inverseWorld)) * localSurfaceNormal
                : glm::vec3(0, 1, 0);
            const glm::vec3 localUp = glm::mat3(inverseWorld) * worldUp;
            const float largestUp = std::max({std::abs(localUp.x), std::abs(localUp.y), std::abs(localUp.z)});
            if (!std::isfinite(largestUp) || largestUp < std::numeric_limits<float>::min()) continue;
            const glm::vec3 normal = glm::normalize(localUp / largestUp);
            // Render transforms use Y * X * Z. Solve tilt after removing yaw
            // so every randomly rotated instance keeps its up axis on the normal.
            const glm::vec3 unyawedNormal = glm::vec3(
                glm::rotate(glm::mat4(1.0f), glm::radians(-yaw), glm::vec3(0, 1, 0)) * glm::vec4(normal, 0));
            type.instances.push_back(FoliageInstance{
                .id = m_nextInstanceId++,
                .position = localPosition,
                .rotationDegrees = {glm::degrees(std::atan2(unyawedNormal.z, unyawedNormal.y)), yaw,
                                    -glm::degrees(std::asin(glm::clamp(unyawedNormal.x, -1.0f, 1.0f)))},
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

    std::size_t FoliageComponent::ApplyInstanceScaleEdit(const FoliageScaleEdit &edit,
                                                         std::optional<std::size_t> typeIndex)
    {
        static constexpr float kMinimumScale = 0.0001f;
        const auto sanitize = [](float value, float fallback)
        {
            return std::isfinite(value) ? (std::max)(value, kMinimumScale) : fallback;
        };
        const float minScale = sanitize((std::min)(edit.minScale, edit.maxScale), 1.0f);
        const float maxScale = sanitize((std::max)(edit.minScale, edit.maxScale), minScale);
        // SplitMix64 over the instance and type: the same range always gives an instance the same scale, and types
        // sharing instance IDs do not vary in step.
        const auto unitRandom = [](std::uint64_t instanceId, std::size_t type)
        {
            std::uint64_t z = instanceId + (static_cast<std::uint64_t>(type) + 1) * 0x9E3779B97F4A7C15ull;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            z ^= z >> 31;
            return static_cast<float>(static_cast<double>(z) / 18446744073709551616.0);
        };

        std::size_t changedCount = 0;
        const std::size_t first = typeIndex.value_or(0);
        const std::size_t end = typeIndex ? (std::min)(first + 1, m_types.size()) : m_types.size();
        for (std::size_t index = first; index < end; ++index)
        {
            for (auto &instance : m_types[index].instances)
            {
                glm::vec3 scale = instance.scale;
                switch (edit.mode)
                {
                case FoliageScaleMode::Multiply:
                {
                    const float factor = sanitize(edit.factor, 1.0f);
                    scale = glm::vec3(sanitize(scale.x * factor, scale.x),
                                      sanitize(scale.y * factor, scale.y),
                                      sanitize(scale.z * factor, scale.z));
                    break;
                }
                case FoliageScaleMode::Set:
                    scale = glm::vec3(sanitize(edit.scale, 1.0f));
                    break;
                case FoliageScaleMode::Randomize:
                    scale = glm::vec3(minScale + (maxScale - minScale) * unitRandom(instance.id, index));
                    break;
                }
                if (scale != instance.scale)
                {
                    instance.scale = scale;
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

    std::optional<FoliageInstanceLocation> FoliageComponent::FindInstance(std::uint64_t instanceId,
                                                                          std::optional<FoliageInstanceLocation> hint) const
    {
        if (hint)
            if (const auto *instance = GetInstance(*hint); instance && instance->id == instanceId)
                return hint;
        for (std::size_t typeIndex = 0; typeIndex < m_types.size(); ++typeIndex)
        {
            const auto &instances = m_types[typeIndex].instances;
            for (std::size_t instanceIndex = 0; instanceIndex < instances.size(); ++instanceIndex)
                if (instances[instanceIndex].id == instanceId)
                    return FoliageInstanceLocation{typeIndex, instanceIndex};
        }
        return std::nullopt;
    }

    const FoliageInstance *FoliageComponent::GetInstance(const FoliageInstanceLocation &location) const
    {
        const auto *type = GetType(location.typeIndex);
        return type && location.instanceIndex < type->instances.size() ? &type->instances[location.instanceIndex] : nullptr;
    }

    std::size_t FoliageComponent::SetInstances(std::span<const FoliageInstance> instances)
    {
        if (instances.empty()) return 0;
        std::unordered_map<std::uint64_t, const FoliageInstance *> replacements;
        replacements.reserve(instances.size());
        for (const auto &instance : instances) replacements[instance.id] = &instance;

        std::size_t changed = 0;
        for (auto &type : m_types)
            for (auto &instance : type.instances)
                if (const auto found = replacements.find(instance.id); found != replacements.end())
                {
                    FoliageInstance replacement = *found->second;
                    replacement.scale = glm::max(replacement.scale, glm::vec3(0.0001f));
                    if (replacement != instance)
                    {
                        instance = replacement;
                        ++changed;
                    }
                }
        if (changed > 0) MarkInstancesDirty();
        return changed;
    }

    std::vector<FoliageRemovedInstance> FoliageComponent::RemoveInstances(std::span<const std::uint64_t> instanceIds)
    {
        std::vector<FoliageRemovedInstance> removed;
        if (instanceIds.empty()) return removed;
        const std::unordered_set<std::uint64_t> ids(instanceIds.begin(), instanceIds.end());
        for (std::size_t typeIndex = 0; typeIndex < m_types.size(); ++typeIndex)
        {
            auto &instances = m_types[typeIndex].instances;
            std::size_t kept = 0;
            for (std::size_t index = 0; index < instances.size(); ++index)
            {
                if (ids.contains(instances[index].id))
                    removed.push_back({{typeIndex, index}, instances[index]});
                else
                {
                    if (kept != index) instances[kept] = instances[index];
                    ++kept;
                }
            }
            instances.resize(kept);
        }
        if (!removed.empty()) MarkInstancesDirty();
        return removed;
    }

    void FoliageComponent::RestoreInstances(std::span<const FoliageRemovedInstance> removed)
    {
        if (removed.empty()) return;
        // Ascending original indices: each insert lands where it was, given the earlier ones are back.
        for (const auto &entry : removed)
        {
            auto *type = GetType(entry.location.typeIndex);
            if (!type) continue;
            const auto index = (std::min)(entry.location.instanceIndex, type->instances.size());
            type->instances.insert(type->instances.begin() + static_cast<std::ptrdiff_t>(index), entry.instance);
            m_nextInstanceId = (std::max)(m_nextInstanceId, entry.instance.id + 1);
        }
        MarkInstancesDirty();
    }

    std::optional<FoliageInstanceHit> FoliageComponent::PickInstance(const glm::vec3 &origin, const glm::vec3 &direction) const
    {
        const auto *owner = GetOwner();
        if (!owner) return std::nullopt;
        const glm::mat4 ownerTransform = owner->GetWorldTransform();

        struct Candidate
        {
            FoliageInstanceLocation location;
            std::uint64_t id = 0;
            float entry = 0.0f;
            float centerMiss = 0.0f; // How far from the sphere's center the ray passes, as a share of its radius.
        };
        std::vector<Candidate> candidates;
        for (std::size_t typeIndex = 0; typeIndex < m_types.size(); ++typeIndex)
        {
            const auto &type = m_types[typeIndex];
            if (!type.mesh) continue;
            const auto submeshes = ResolveSelectedSubmeshes(type);
            const auto bounds = CombineSubmeshBounds(*type.mesh, submeshes);
            const glm::vec3 originOffset = ComputeSubmeshGroupCenterOfMass(*type.mesh, submeshes);
            // Reaches every instance's sphere from its pivot, so most instances are rejected without a matrix.
            const float pivotReach = glm::length(bounds.center - originOffset) + bounds.radius;
            for (std::size_t instanceIndex = 0; instanceIndex < type.instances.size(); ++instanceIndex)
            {
                const auto &instance = type.instances[instanceIndex];
                const glm::vec3 pivot = glm::vec3(ownerTransform * glm::vec4(instance.position, 1.0f));
                const float largestScale = (std::max)({std::abs(instance.scale.x), std::abs(instance.scale.y), std::abs(instance.scale.z)});
                const float reach = pivotReach * largestScale;
                const glm::vec3 toPivot = pivot - origin;
                const float along = glm::dot(toPivot, direction);
                if (along < -reach || glm::length2(toPivot - direction * along) > reach * reach) continue;

                const auto worldBounds = TransformBounds(bounds, ComposeInstanceTransform(ownerTransform, instance, originOffset));
                const glm::vec3 toCenter = worldBounds.center - origin;
                const float centerAlong = glm::dot(toCenter, direction);
                const float missSquared = glm::length2(toCenter - direction * centerAlong);
                const float radiusSquared = worldBounds.radius * worldBounds.radius;
                if (missSquared > radiusSquared) continue;
                const float entry = centerAlong - std::sqrt(radiusSquared - missSquared);
                if (entry + 2.0f * worldBounds.radius < 0.0f) continue; // Wholly behind the ray.
                candidates.push_back({{typeIndex, instanceIndex}, instance.id, (std::max)(entry, 0.0f),
                                      radiusSquared > 0.0f ? std::sqrt(missSquared / radiusSquared) : 1.0f});
            }
        }
        if (candidates.empty()) return std::nullopt;
        std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) { return a.entry < b.entry; });

        // Triangles decide among the nearest spheres, which overlap heavily in a forest.
        constexpr std::size_t kExactCandidates = 24;
        constexpr std::size_t kMaxExactTriangles = 250000;
        std::optional<FoliageInstanceHit> best;
        const std::size_t exactCount = (std::min)(candidates.size(), kExactCandidates);
        for (std::size_t index = 0; index < exactCount; ++index)
        {
            const auto &candidate = candidates[index];
            if (best && candidate.entry > best->distance) break;
            const auto &type = m_types[candidate.location.typeIndex];
            const auto &meshData = type.mesh->GetMeshData();
            if (meshData.vertices.empty()) continue;
            const auto submeshes = ResolveSelectedSubmeshes(type);
            const glm::vec3 originOffset = ComputeSubmeshGroupCenterOfMass(*type.mesh, submeshes);
            const glm::mat4 inverse = glm::inverse(ComposeInstanceTransform(ownerTransform, type.instances[candidate.location.instanceIndex], originOffset));
            const glm::vec3 localOrigin = glm::vec3(inverse * glm::vec4(origin, 1.0f));
            // Not renormalized, so the hit parameter stays a world distance.
            const glm::vec3 localDirection = glm::vec3(inverse * glm::vec4(direction, 0.0f));
            for (const std::size_t submeshIndex : submeshes)
            {
                const auto &submesh = type.mesh->GetSubmesh(submeshIndex);
                if (submesh.indexCount / 3 > kMaxExactTriangles || submesh.indexOffset + submesh.indexCount > meshData.indices.size())
                    continue;
                const std::size_t end = submesh.indexOffset + submesh.indexCount;
                for (std::size_t triangle = submesh.indexOffset; triangle + 2 < end; triangle += 3)
                {
                    const auto a = meshData.indices[triangle], b = meshData.indices[triangle + 1], c = meshData.indices[triangle + 2];
                    if (a >= meshData.vertices.size() || b >= meshData.vertices.size() || c >= meshData.vertices.size()) continue;
                    const auto position = [&](auto vertex)
                    {
                        const auto &p = meshData.vertices[vertex].position;
                        return glm::vec3(p[0], p[1], p[2]);
                    };
                    const auto hit = IntersectRayTriangle(localOrigin, localDirection, position(a), position(b), position(c));
                    if (hit && (!best || *hit < best->distance))
                        best = FoliageInstanceHit{candidate.id, candidate.location, *hit};
                }
            }
        }
        if (best) return best;

        const auto closest = std::min_element(candidates.begin(), candidates.end(),
            [](const Candidate &a, const Candidate &b) { return a.centerMiss < b.centerMiss; });
        return FoliageInstanceHit{closest->id, closest->location, closest->entry};
    }

    std::optional<FoliageInstanceBounds> FoliageComponent::GetInstanceBounds(const FoliageInstanceLocation &location) const
    {
        const auto *type = GetType(location.typeIndex);
        const auto *instance = GetInstance(location);
        const auto *owner = GetOwner();
        if (!type || !instance || !owner) return std::nullopt;

        FoliageInstanceBounds result;
        render::MeshBounds sphere{};
        glm::vec3 originOffset(0.0f);
        if (type->mesh)
        {
            const auto submeshes = ResolveSelectedSubmeshes(*type);
            sphere = CombineSubmeshBounds(*type->mesh, submeshes);
            originOffset = ComputeSubmeshGroupCenterOfMass(*type->mesh, submeshes);
            result.localMin = sphere.center - glm::vec3(sphere.radius);
            result.localMax = sphere.center + glm::vec3(sphere.radius);
            bool exact = !submeshes.empty();
            glm::vec3 low(std::numeric_limits<float>::max());
            glm::vec3 high(-std::numeric_limits<float>::max());
            for (const std::size_t submeshIndex : submeshes)
            {
                const auto &submesh = type->mesh->GetSubmesh(submeshIndex);
                exact = exact && submesh.hasBoundsExtents;
                low = glm::min(low, submesh.boundsMin);
                high = glm::max(high, submesh.boundsMax);
            }
            if (exact)
            {
                result.localMin = low;
                result.localMax = high;
            }
        }
        else
        {
            // Without a mesh there is still a pivot to show.
            sphere.radius = 0.5f;
            result.localMin = glm::vec3(-0.5f);
            result.localMax = glm::vec3(0.5f);
        }
        result.model = ComposeInstanceTransform(owner->GetWorldTransform(), *instance, originOffset);
        const auto world = TransformBounds(sphere, result.model);
        result.worldCenter = world.center;
        result.worldRadius = world.radius;
        return result;
    }

    std::optional<glm::mat4> FoliageComponent::GetInstancePivotTransform(const FoliageInstanceLocation &location) const
    {
        const auto *instance = GetInstance(location);
        const auto *owner = GetOwner();
        if (!instance || !owner) return std::nullopt;
        return ComposeInstanceTransform(owner->GetWorldTransform(), *instance, glm::vec3(0.0f), false);
    }

    std::optional<glm::mat4> FoliageComponent::GetInstanceWorldTransform(const FoliageInstanceLocation &location) const
    {
        const auto *instance = GetInstance(location);
        const auto *owner = GetOwner();
        if (!instance || !owner) return std::nullopt;
        return ComposeInstanceTransform(owner->GetWorldTransform(), *instance);
    }

    std::vector<FoliageInstanceLocation> FoliageComponent::FindInstancesInRadius(const glm::vec3 &worldCenter, float radius) const
    {
        std::vector<FoliageInstanceLocation> found;
        const auto *owner = GetOwner();
        if (!owner || !(radius >= 0.0f)) return found;
        const glm::mat4 ownerTransform = owner->GetWorldTransform();
        const float radiusSquared = radius * radius;
        std::vector<float> distances;
        for (std::size_t typeIndex = 0; typeIndex < m_types.size(); ++typeIndex)
        {
            const auto &instances = m_types[typeIndex].instances;
            for (std::size_t instanceIndex = 0; instanceIndex < instances.size(); ++instanceIndex)
            {
                const glm::vec3 pivot = glm::vec3(ownerTransform * glm::vec4(instances[instanceIndex].position, 1.0f));
                const float distanceSquared = glm::length2(pivot - worldCenter);
                if (distanceSquared > radiusSquared) continue;
                found.push_back({typeIndex, instanceIndex});
                distances.push_back(distanceSquared);
            }
        }
        std::vector<std::size_t> order(found.size());
        for (std::size_t index = 0; index < order.size(); ++index) order[index] = index;
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return distances[a] < distances[b]; });
        std::vector<FoliageInstanceLocation> sorted;
        sorted.reserve(found.size());
        for (const auto index : order) sorted.push_back(found[index]);
        return sorted;
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

    FoliageInstanceSnapshot FoliageComponent::CaptureInstanceSnapshot() const
    {
        FoliageInstanceSnapshot snapshot;
        snapshot.reserve(m_types.size());
        for (const auto &type : m_types)
            snapshot.push_back(type.instances);
        return snapshot;
    }

    void FoliageComponent::RestoreInstanceSnapshot(const FoliageInstanceSnapshot &snapshot)
    {
        const std::size_t typeCount = std::min(m_types.size(), snapshot.size());
        for (std::size_t typeIndex = 0; typeIndex < typeCount; ++typeIndex)
            m_types[typeIndex].instances = snapshot[typeIndex];
        for (std::size_t typeIndex = typeCount; typeIndex < m_types.size(); ++typeIndex)
            m_types[typeIndex].instances.clear();
        EnsureStableInstanceIds();
        MarkInstancesDirty();
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
            cell.signature = ComputeCollisionCellSignature(cell);
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
                type->asset.submeshIndex = -1;
                type->asset.submeshIndices.clear();
            }
            else
            {
                const int maxSubmeshIndex = static_cast<int>((std::max<std::size_t>)(type->mesh->GetSubmeshCount(), 1) - 1);
                type->asset.submeshIndex = std::clamp(submeshIndex, 0, maxSubmeshIndex);
                type->asset.submeshIndices = {type->asset.submeshIndex};
            }
            MarkRenderCommandsDirty();
        }
    }

    void FoliageComponent::SetTypeSubmeshIndices(std::size_t index, const std::vector<int> &submeshIndices)
    {
        if (auto *type = GetType(index))
        {
            type->asset.submeshIndices.clear();
            if (!type->mesh || submeshIndices.empty())
            {
                type->asset.submeshIndex = -1;
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
                if (std::find(type->asset.submeshIndices.begin(), type->asset.submeshIndices.end(), clampedIndex) == type->asset.submeshIndices.end())
                {
                    type->asset.submeshIndices.push_back(clampedIndex);
                }
            }

            type->asset.submeshIndex = type->asset.submeshIndices.size() == 1 ? type->asset.submeshIndices.front() : -1;
            MarkRenderCommandsDirty();
        }
    }

    void FoliageComponent::SetTypeAlignToTerrainNormal(std::size_t index, bool align)
    {
        if (auto *type = GetType(index)) type->asset.alignToTerrainNormal = align;
        // Placement policy only: existing instance transforms stay authored.
    }

    void FoliageComponent::SetTypeUseGeneratedLods(std::size_t index, bool useGeneratedLods)
    {
        if (auto *type = GetType(index))
        {
            if (type->asset.useGeneratedLods == useGeneratedLods)
            {
                return;
            }

            type->asset.useGeneratedLods = useGeneratedLods;
            RebuildTypeMeshFromReference(*type);
            RebuildTypeMaterialFromReference(*type);
            MarkRenderCommandsDirty();
        }
    }

    void FoliageComponent::SetTypeSourceMeshPath(std::size_t index, const std::string &sourceMeshPath)
    {
        if (auto *type = GetType(index))
        {
            type->asset.sourceMeshPath = sourceMeshPath;
            const auto &metadata = core::Engine::GetInstance().GetAssetManager().GetMeshAssetMetadata(sourceMeshPath);
            type->asset.modelAssetId = metadata.sourceAssetId;
            type->asset.modelObjectId = metadata.sourceObjectId;
            RebuildTypeMeshFromReference(*type);
            RebuildTypeMaterialFromReference(*type);
            MarkRenderCommandsDirty();
        }
    }

    void FoliageComponent::SetTypeMaterialAssetReference(std::size_t index, const std::string &materialAssetReference)
    {
        if (auto *type = GetType(index))
        {
            type->asset.materialAssetReference = materialAssetReference;
            RebuildTypeMaterialFromReference(*type);
            MarkRenderCommandsDirty();
        }
    }

    void FoliageComponent::ClearTypeMaterialAssetReference(std::size_t index)
    {
        if (auto *type = GetType(index))
        {
            type->asset.materialAssetReference.clear();
            type->materialOverride = nullptr;
            MarkRenderCommandsDirty();
        }
    }

    bool FoliageComponent::SetTypeAssetReference(std::size_t index, const std::string &assetReference, std::string *errorMessage)
    {
        auto *type = GetType(index);
        if (!type) return false;
        if (assetReference.empty())
        {
            type->asset.assetReference.clear();
            type->assetLoadError.clear();
            return true;
        }
        FoliageTypeAsset loaded = type->asset;
        loaded.assetReference = assetReference;
        if (!LoadFoliageTypeAsset(core::Engine::GetInstance().GetAssetManager().ResolveAssetPath(assetReference), loaded, errorMessage))
            return false;
        type->assetLoadError.clear();
        type->asset.assetReference = assetReference;
        ApplyTypeAsset(index, std::move(loaded));
        return true;
    }

    void FoliageComponent::ApplyTypeAsset(std::size_t index, FoliageTypeAsset asset)
    {
        auto *type = GetType(index);
        if (!type) return;
        asset.assetReference = type->asset.assetReference;
        SanitizeFoliageTypeAsset(asset);
        if (asset == type->asset) return;

        const FoliageTypeAsset before = std::move(type->asset);
        type->asset = std::move(asset);
        auto &current = type->asset;
        const bool meshChanged = before.sourceMeshPath != current.sourceMeshPath ||
                                 before.useGeneratedLods != current.useGeneratedLods;
        if (before.sourceMeshPath != current.sourceMeshPath)
        {
            const auto &metadata = core::Engine::GetInstance().GetAssetManager().GetMeshAssetMetadata(current.sourceMeshPath);
            current.modelAssetId = metadata.sourceAssetId;
            current.modelObjectId = metadata.sourceObjectId;
        }
        if (meshChanged)
        {
            // An import can fail on a bad file. The type is left without a mesh, which the inspector reports, rather
            // than failing whatever edit got here.
            try
            {
                RebuildTypeMeshFromReference(*type);
            }
            catch (const std::exception &)
            {
                type->mesh = nullptr;
            }
        }
        if (meshChanged || before.materialAssetReference != current.materialAssetReference)
            RebuildTypeMaterialFromReference(*type);
        if (!meshChanged && (before.submeshIndex != current.submeshIndex || before.submeshIndices != current.submeshIndices))
            SanitizeTypeSubmeshSelection(*type);

        // Cells and capsules feed collision, which follows the instance revision.
        const bool instancesAffected = before.cellSize != current.cellSize || before.collisionEnabled != current.collisionEnabled ||
                                       before.collisionCenter != current.collisionCenter ||
                                       before.collisionRadius != current.collisionRadius ||
                                       before.collisionHeight != current.collisionHeight;
        if (instancesAffected) MarkInstancesDirty();
        else MarkRenderCommandsDirty();
    }

    std::size_t FoliageComponent::ReloadTypeAssets(const std::string &assetReference)
    {
        if (assetReference.empty()) return 0;
        const auto path = core::Engine::GetInstance().GetAssetManager().ResolveAssetPath(assetReference);
        std::size_t reloaded = 0;
        for (std::size_t index = 0; index < m_types.size(); ++index)
        {
            auto &type = m_types[index];
            if (type.asset.assetReference != assetReference) continue;
            ++reloaded;
            FoliageTypeAsset loaded = type.asset;
            if (LoadFoliageTypeAsset(path, loaded, &type.assetLoadError))
            {
                type.assetLoadError.clear();
                ApplyTypeAsset(index, std::move(loaded));
            }
        }
        return reloaded;
    }

    std::size_t FoliageComponent::PreviewTypeAsset(const std::string &assetReference, const FoliageTypeAsset &asset)
    {
        if (assetReference.empty()) return 0;
        std::size_t previewed = 0;
        for (std::size_t index = 0; index < m_types.size(); ++index)
        {
            if (m_types[index].asset.assetReference != assetReference) continue;
            ++previewed;
            ApplyTypeAsset(index, asset);
        }
        return previewed;
    }

    void FoliageComponent::SetTypeScaleRange(std::size_t index, float minScale, float maxScale)
    {
        if (auto *type = GetType(index))
        {
            type->asset.minScale = minScale;
            type->asset.maxScale = maxScale;
            SanitizeFoliageTypeAsset(type->asset);
        }
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

    void FoliageComponent::SetTypeLodDistanceScale(std::size_t index, float scale)
    {
        if (auto *type = GetType(index))
        {
            const float sanitized = render::SanitizeLodDistanceScale(scale);
            if (type->asset.lodDistanceScale == sanitized)
                return;
            type->asset.lodDistanceScale = sanitized;
            MarkRenderCommandsDirty();
        }
    }

    void FoliageComponent::SetTypeWind(std::size_t index, const render::FoliageWindResponse &wind)
    {
        if (auto *type = GetType(index))
        {
            const auto sanitized = wind.Sanitized();
            if (type->asset.wind == sanitized)
                return;
            type->asset.wind = sanitized;
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
            type->asset.sourceMeshPath = sourceMeshPath;
            SanitizeTypeSubmeshSelection(*type);
            if (type->asset.materialAssetReference.empty())
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

    void FoliageComponent::SyncTypeMaterials(std::size_t index, const std::vector<render::Material *> &materials)
    {
        auto *type = GetType(index);
        if (!type) return;
        bool sameLayout = type->materials.size() == materials.size();
        for (std::size_t slot = 0; sameLayout && slot < materials.size(); ++slot)
            sameLayout = (type->materials[slot] != nullptr) == (materials[slot] != nullptr);
        if (sameLayout)
        {
            for (std::size_t slot = 0; slot < materials.size(); ++slot)
                if (materials[slot])
                    *type->materials[slot] = render::Material(FoliageMaterialConfig(materials[slot]->ReadConfig()));
        }
        else
        {
            AssignFoliageMaterials(*type, materials);
        }
        MarkRenderCommandsDirty();
    }

    std::size_t FoliageComponent::RefreshMaterialAsset(const render::Material *material)
    {
        if (!material) return 0;
        auto &assets = core::Engine::GetInstance().GetAssetManager();
        std::size_t refreshed = 0;
        for (std::size_t index = 0; index < m_types.size(); ++index)
        {
            auto &type = m_types[index];
            if (type.materialOverride == material)
            {
                MarkRenderCommandsDirty();
                ++refreshed;
            }
            if (type.asset.sourceMeshPath.empty() || !type.mesh) continue;
            const auto references = assets.GetMeshAssetMaterialReferences(type.asset.sourceMeshPath);
            std::vector<render::Material *> current;
            current.reserve(references.size());
            bool uses = false;
            for (const auto &reference : references)
            {
                current.push_back(assets.LoadMaterialAsset(reference));
                uses = uses || current.back() == material;
            }
            if (!uses) continue;
            SyncTypeMaterials(index, current);
            ++refreshed;
        }
        return refreshed;
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
        ++m_instanceTextRevision;
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
        if (type.asset.materialAssetReference.empty())
        {
            type.materialOverride = nullptr;
            if (!m_material)
            {
                m_material = core::Engine::GetInstance().GetAssetManager().LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference));
            }
            return;
        }

        type.materialOverride = core::Engine::GetInstance().GetAssetManager().LoadMaterialAsset(type.asset.materialAssetReference);
    }

    void FoliageComponent::RebuildTypeMeshFromReference(FoliageType &type)
    {
        type.mesh = nullptr;
        type.materials.clear();
        type.ownedMaterials.clear();
        type.impostor = nullptr;
        if (type.asset.sourceMeshPath.empty())
        {
            return;
        }

        auto &engine = core::Engine::GetInstance();
        if (auto *mesh = engine.GetAssetManager().LoadMeshAsset(type.asset.sourceMeshPath))
        {
            type.mesh = mesh;
            const auto &materialReferences = engine.GetAssetManager().GetMeshAssetMaterialReferences(type.asset.sourceMeshPath);
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
            type.impostor = LoadImpostor(engine.GetAssetManager(), type.asset.sourceMeshPath);
            SanitizeTypeSubmeshSelection(type);
            return;
        }

        const std::string resolvedPath = engine.GetAssetManager().ResolveMeshAssetSourcePath(type.asset.sourceMeshPath);
        const auto options = SourceModelImportOptions(resolvedPath);
        auto imported = type.asset.useGeneratedLods || options.generateLods ? engine.GenerateMeshAssetLods(resolvedPath, options)
                                                                      : engine.ImportMeshAsset(resolvedPath, options);
        type.mesh = imported.mesh;
        AssignFoliageMaterials(type, imported.materials);
        SanitizeTypeSubmeshSelection(type);
    }
}
