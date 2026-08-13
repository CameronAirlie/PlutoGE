#include "PlutoGE/scene/PrefabMeshExporter.h"

#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/MeshComponent.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <unordered_map>

#include <glm/gtc/matrix_inverse.hpp>

namespace PlutoGE::scene
{
    namespace
    {
        void SetError(std::string *error, std::string value)
        {
            if (error) *error = std::move(value);
        }

        std::string ResolveMaterialReference(const MeshComponent &component,
                                             std::size_t submeshIndex,
                                             assets::AssetManager &assetManager)
        {
            if (const auto &overrideReference = component.GetMaterialAssetForSubmesh(submeshIndex);
                !overrideReference.empty())
                return overrideReference;

            const auto *mesh = component.GetMesh();
            if (!mesh || submeshIndex >= mesh->GetSubmeshCount()) return {};
            const auto materialSlot = static_cast<std::size_t>(mesh->GetSubmesh(submeshIndex).materialIndex);
            if (const auto &slotReference = component.GetMaterialAssetForMaterialSlot(materialSlot);
                !slotReference.empty())
                return slotReference;

            if (!component.GetMeshAssetReference().empty())
            {
                const auto &sourceReferences = assetManager.GetMeshAssetMaterialReferences(component.GetMeshAssetReference());
                if (materialSlot < sourceReferences.size()) return sourceReferences[materialSlot];
            }
            return component.GetMaterialForSubmesh(submeshIndex)
                       ? std::string{}
                       : std::string(assets::Project::kBuiltinDefaultShadedMaterialReference);
        }

        std::uint32_t GetOrAddMaterial(PrefabMeshExportData &output,
                                       std::string reference,
                                       const render::Material *material)
        {
            if (!reference.empty())
            {
                const auto found = std::find(output.materialReferences.begin(), output.materialReferences.end(), reference);
                if (found != output.materialReferences.end())
                    return static_cast<std::uint32_t>(std::distance(output.materialReferences.begin(), found));
            }
            output.materialReferences.push_back(std::move(reference));
            output.embeddedMaterialConfigs.push_back(material ? material->GetConfig() : render::MaterialConfig{});
            output.hasEmbeddedMaterial.push_back(material ? 1 : 0);
            return static_cast<std::uint32_t>(output.materialReferences.size() - 1);
        }

        bool AppendMeshComponent(const Entity &entity,
                                 const MeshComponent &component,
                                 const glm::mat4 &rootInverse,
                                 assets::AssetManager &assetManager,
                                 PrefabMeshExportData &output,
                                 std::string *errorMessage)
        {
            const auto *mesh = component.GetMesh();
            if (!mesh) return true;
            if (mesh->HasSkeleton())
            {
                SetError(errorMessage, "Cannot export skeletal mesh component on entity '" + entity.GetName() + "' as a static prefab mesh.");
                return false;
            }

            const auto &sourceData = mesh->GetMeshData();
            const std::size_t submeshBegin = component.GetSubmeshIndex() >= 0
                                                 ? static_cast<std::size_t>(component.GetSubmeshIndex())
                                                 : 0;
            const std::size_t submeshEnd = component.GetSubmeshIndex() >= 0
                                               ? (std::min)(submeshBegin + static_cast<std::size_t>((std::max)(1, component.GetSubmeshRangeCount())), mesh->GetSubmeshCount())
                                               : mesh->GetSubmeshCount();
            if (submeshBegin >= submeshEnd) return true;

            for (std::size_t submeshIndex = submeshBegin; submeshIndex < submeshEnd; ++submeshIndex)
            {
                const auto &sourceSubmesh = mesh->GetSubmesh(submeshIndex);
                if (sourceSubmesh.animatedNodeIndex >= 0)
                {
                    SetError(errorMessage, "Cannot export animated-node submesh '" + sourceSubmesh.name + "' as a static prefab mesh.");
                    return false;
                }
                if (sourceSubmesh.indexOffset + sourceSubmesh.indexCount > sourceData.indices.size())
                {
                    SetError(errorMessage, "Prefab contains a mesh with an invalid submesh index range.");
                    return false;
                }

                const glm::mat4 transform = rootInverse * entity.GetWorldTransform() *
                                            component.GetMeshOffsetTransform() *
                                            component.GetSubmeshOffsetTransform(submeshIndex);
                const glm::mat3 normalTransform = glm::inverseTranspose(glm::mat3(transform));
                const glm::mat3 directionTransform(transform);
                const bool mirrored = glm::determinant(directionTransform) < 0.0f;
                std::unordered_map<unsigned int, unsigned int> vertexMap;
                vertexMap.reserve(sourceSubmesh.indexCount);

                render::Submesh exportedSubmesh;
                exportedSubmesh.indexOffset = static_cast<std::uint32_t>(output.mesh.data.indices.size());
                exportedSubmesh.materialIndex = GetOrAddMaterial(
                    output,
                    ResolveMaterialReference(component, submeshIndex, assetManager),
                    component.GetMaterialForSubmesh(submeshIndex));
                exportedSubmesh.name = entity.GetName();
                if (!sourceSubmesh.name.empty()) exportedSubmesh.name += "/" + sourceSubmesh.name;

                auto appendIndex = [&](unsigned int sourceIndex) -> bool
                {
                    if (sourceIndex >= sourceData.vertices.size()) return false;
                    auto [iterator, inserted] = vertexMap.emplace(sourceIndex, 0);
                    if (inserted)
                    {
                        auto vertex = sourceData.vertices[sourceIndex];
                        const glm::vec3 position = glm::vec3(transform * glm::vec4(
                            vertex.position[0], vertex.position[1], vertex.position[2], 1.0f));
                        const glm::vec3 normal = glm::normalize(normalTransform * glm::vec3(
                            vertex.normal[0], vertex.normal[1], vertex.normal[2]));
                        const glm::vec3 tangent = glm::normalize(directionTransform * glm::vec3(
                            vertex.tangent[0], vertex.tangent[1], vertex.tangent[2]));
                        vertex.position = {position.x, position.y, position.z};
                        vertex.normal = {normal.x, normal.y, normal.z};
                        vertex.tangent = {tangent.x, tangent.y, tangent.z, mirrored ? -vertex.tangent[3] : vertex.tangent[3]};
                        vertex.joints = {0, 0, 0, 0};
                        vertex.weights = {0, 0, 0, 0};
                        iterator->second = static_cast<unsigned int>(output.mesh.data.vertices.size());
                        output.mesh.data.vertices.push_back(vertex);
                    }
                    output.mesh.data.indices.push_back(iterator->second);
                    return true;
                };

                for (std::uint32_t offset = 0; offset + 2 < sourceSubmesh.indexCount; offset += 3)
                {
                    const auto base = sourceSubmesh.indexOffset + offset;
                    const unsigned int triangle[3] = {
                        sourceData.indices[base], sourceData.indices[base + 1], sourceData.indices[base + 2]};
                    const int order[3] = {0, mirrored ? 2 : 1, mirrored ? 1 : 2};
                    if (!appendIndex(triangle[order[0]]) || !appendIndex(triangle[order[1]]) || !appendIndex(triangle[order[2]]))
                    {
                        SetError(errorMessage, "Prefab contains a mesh with an invalid vertex index.");
                        return false;
                    }
                }
                exportedSubmesh.indexCount = static_cast<std::uint32_t>(output.mesh.data.indices.size()) - exportedSubmesh.indexOffset;
                exportedSubmesh.lods.push_back({.indexOffset = exportedSubmesh.indexOffset, .indexCount = exportedSubmesh.indexCount});
                if (exportedSubmesh.indexCount > 0) output.mesh.submeshes.push_back(std::move(exportedSubmesh));
            }
            ++output.meshComponentCount;
            return true;
        }

        bool Collect(const Entity &entity,
                     const glm::mat4 &rootInverse,
                     assets::AssetManager &assetManager,
                     PrefabMeshExportData &output,
                     std::string *errorMessage)
        {
            if (!entity.IsSelfActive()) return true;
            for (auto *component : entity.GetComponents<MeshComponent>())
                if (component && component->IsEnabled() &&
                    !AppendMeshComponent(entity, *component, rootInverse, assetManager, output, errorMessage))
                    return false;
            for (const auto *child : entity.GetChildren())
                if (child && !Collect(*child, rootInverse, assetManager, output, errorMessage)) return false;
            return true;
        }
    }

    bool BuildStaticMeshFromEntityHierarchy(const Entity &root,
                                            assets::AssetManager &assetManager,
                                            PrefabMeshExportData &output,
                                            std::string *errorMessage)
    {
        output = {};
        if (!Collect(root, glm::inverse(root.GetWorldTransform()), assetManager, output, errorMessage)) return false;
        output.submeshCount = output.mesh.submeshes.size();
        // Separate source meshes can reuse the same secondary UV space, so the
        // flattened result needs a new atlas before it may advertise lightmap UVs.
        output.mesh.hasLightmapUvs = false;
        if (output.mesh.data.vertices.empty() || output.mesh.data.indices.empty() || output.mesh.submeshes.empty())
        {
            SetError(errorMessage, "Prefab contains no enabled static mesh components.");
            return false;
        }
        return true;
    }

    bool ExportPrefabToStaticMeshAsset(const std::string &prefabReference,
                                       const std::string &meshAssetReference,
                                       assets::AssetManager &assetManager,
                                       std::string *errorMessage)
    {
        Scene temporaryScene;
        auto *root = Prefab::Instantiate(temporaryScene, prefabReference, nullptr, errorMessage);
        if (!root) return false;
        PrefabMeshExportData exportData;
        if (!BuildStaticMeshFromEntityHierarchy(*root, assetManager, exportData, errorMessage)) return false;
        const std::filesystem::path meshPath(assetManager.ResolveAssetPath(meshAssetReference));
        for (std::size_t materialIndex = 0; materialIndex < exportData.materialReferences.size(); ++materialIndex)
        {
            if (!exportData.materialReferences[materialIndex].empty()) continue;
            if (materialIndex >= exportData.hasEmbeddedMaterial.size() || !exportData.hasEmbeddedMaterial[materialIndex])
            {
                exportData.materialReferences[materialIndex] = std::string(assets::Project::kBuiltinDefaultShadedMaterialReference);
                continue;
            }
            const auto materialPath = meshPath.parent_path() /
                (meshPath.stem().string() + "_Material_" + std::to_string(materialIndex) + ".plutomaterial");
            const std::string materialReference = assetManager.PersistAssetPath(materialPath.string());
            if (!assetManager.SaveMaterialAsset(materialReference, exportData.embeddedMaterialConfigs[materialIndex], errorMessage))
                return false;
            exportData.materialReferences[materialIndex] = materialReference;
        }
        assets::MeshAssetMetadata metadata;
        metadata.sourceAssetReference = prefabReference;
        return assetManager.SaveMeshAsset(meshAssetReference, exportData.mesh, exportData.materialReferences, errorMessage, metadata);
    }
}
