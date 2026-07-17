#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/SkeletonAttachmentComponent.h"

#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Texture.h"

#include "PlutoGE/core/Engine.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <memory>
#include <string>

namespace PlutoGE::scene
{
    namespace
    {
        constexpr const char *kMaterialSlotPrefix = "MaterialSlots.";
        constexpr const char *kSubmeshOverridePrefix = "SubmeshOverrides.";

        render::MeshBounds ComputeWorldBounds(const render::Mesh &mesh, std::size_t submeshIndex, const glm::mat4 &modelMatrix)
        {
            const auto &bounds = submeshIndex < mesh.GetSubmeshCount()
                                     ? mesh.GetSubmesh(submeshIndex).bounds
                                     : mesh.GetBounds();
            const glm::vec3 worldCenter = glm::vec3(modelMatrix * glm::vec4(bounds.center, 1.0f));
            const float scaleX = glm::length(glm::vec3(modelMatrix[0]));
            const float scaleY = glm::length(glm::vec3(modelMatrix[1]));
            const float scaleZ = glm::length(glm::vec3(modelMatrix[2]));

            return render::MeshBounds{
                .center = worldCenter,
                .radius = bounds.radius * std::max(scaleX, std::max(scaleY, scaleZ)),
            };
        }

        std::string BuildSkeletonAttachmentEntityName(const render::SkeletonJoint &joint, size_t jointIndex)
        {
            std::string childName = "Joint " + std::to_string(jointIndex);
            if (!joint.name.empty())
            {
                childName += " - " + joint.name;
            }
            return childName;
        }

        bool AreMatricesApproximatelyEqual(const glm::mat4 &a, const glm::mat4 &b)
        {
            constexpr float epsilon = 0.0001f;
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

            return false;
        }

        AnimationComponent *FindAnimationComponent(Entity *entity)
        {
            for (auto *current = entity; current != nullptr; current = current->GetParent())
            {
                if (auto *animationComponent = current->GetComponent<AnimationComponent>())
                {
                    return animationComponent;
                }
            }

            return nullptr;
        }

        glm::mat4 ComputeAnimationNodeBindMatrix(const std::vector<render::AnimationNode> &nodes, int nodeIndex)
        {
            if (nodeIndex < 0 || nodeIndex >= static_cast<int>(nodes.size()))
            {
                return glm::mat4(1.0f);
            }

            std::vector<int> chain;
            for (int currentNodeIndex = nodeIndex;
                 currentNodeIndex >= 0 && currentNodeIndex < static_cast<int>(nodes.size());
                 currentNodeIndex = nodes[static_cast<size_t>(currentNodeIndex)].parentNodeIndex)
            {
                chain.push_back(currentNodeIndex);
            }

            glm::mat4 transform(1.0f);
            for (auto iterator = chain.rbegin(); iterator != chain.rend(); ++iterator)
            {
                transform *= nodes[static_cast<size_t>(*iterator)].localBindTransform;
            }
            return transform;
        }

        std::string SerializeVec4(const glm::vec4 &value)
        {
            return std::to_string(value.r) + "," + std::to_string(value.g) + "," + std::to_string(value.b) + "," + std::to_string(value.a);
        }

        std::string SerializeVec3(const glm::vec3 &value)
        {
            return std::to_string(value.r) + "," + std::to_string(value.g) + "," + std::to_string(value.b);
        }

        std::string SerializeVec2(const glm::vec2 &value)
        {
            return std::to_string(value.x) + "," + std::to_string(value.y);
        }

        glm::vec4 ParseVec4(const std::string &value, const glm::vec4 &fallback = glm::vec4(1.0f))
        {
            glm::vec4 parsedValue = fallback;
            std::sscanf(value.c_str(), "%f,%f,%f,%f", &parsedValue.r, &parsedValue.g, &parsedValue.b, &parsedValue.a);
            return parsedValue;
        }

        glm::vec3 ParseVec3(const std::string &value, const glm::vec3 &fallback = glm::vec3(1.0f))
        {
            glm::vec3 parsedValue = fallback;
            std::sscanf(value.c_str(), "%f,%f,%f", &parsedValue.r, &parsedValue.g, &parsedValue.b);
            return parsedValue;
        }

        glm::vec2 ParseVec2(const std::string &value, const glm::vec2 &fallback = glm::vec2(1.0f))
        {
            glm::vec2 parsedValue = fallback;
            std::sscanf(value.c_str(), "%f,%f", &parsedValue.x, &parsedValue.y);
            return parsedValue;
        }

        const char *ToString(render::MaterialSurfaceType surfaceType)
        {
            return surfaceType == render::MaterialSurfaceType::Glass ? "Glass" : "Standard";
        }

        render::MaterialSurfaceType ParseSurfaceType(const std::string &value)
        {
            return value == "Glass" || value == "glass" || value == "1"
                       ? render::MaterialSurfaceType::Glass
                       : render::MaterialSurfaceType::Standard;
        }

        struct SerializedMaterialData
        {
            std::optional<std::string> materialAsset;
            std::optional<std::string> albedoPath;
            std::optional<glm::vec4> color;
            std::optional<render::MaterialSurfaceType> surfaceType;
            std::optional<render::AlphaMode> alphaMode;
            std::optional<float> alphaCutoff;
            std::optional<bool> castsShadow;
            std::optional<glm::vec2> uvScale;
            std::optional<float> metallic;
            std::optional<float> roughness;
            std::optional<glm::vec3> emission;
            std::optional<float> transmission;
            std::optional<float> ior;
            std::optional<float> thickness;
            std::optional<glm::vec3> attenuationColor;
            std::optional<float> attenuationDistance;
            std::optional<bool> flipNormalY;
            std::optional<std::string> lightmapPath;
        };

        void SerializeInlineMaterialProperties(std::vector<Property> &properties, const std::string &prefix, const render::MaterialConfig &config)
        {
            properties.push_back({prefix + "Color", PropertyType::String, SerializeVec4(config.color)});
            properties.push_back({prefix + "AlbedoPath", PropertyType::String, config.albedoTexture ? config.albedoTexture->GetFilePath() : std::string{}});
            properties.push_back({prefix + "SurfaceType", PropertyType::String, ToString(config.surfaceType)});
            properties.push_back({prefix + "AlphaMode", PropertyType::String, config.alphaMode == render::AlphaMode::Blend ? "Blend" : config.alphaMode == render::AlphaMode::Mask ? "Mask" : "Opaque"});
            properties.push_back({prefix + "AlphaCutoff", PropertyType::Float, std::to_string(config.alphaCutoff)});
            properties.push_back({prefix + "CastsShadow", PropertyType::Bool, config.castsShadow ? "true" : "false"});
            properties.push_back({prefix + "UvScale", PropertyType::String, SerializeVec2(config.uvScale)});
            properties.push_back({prefix + "Metallic", PropertyType::Float, std::to_string(config.metallic)});
            properties.push_back({prefix + "Roughness", PropertyType::Float, std::to_string(config.roughness)});
            properties.push_back({prefix + "Emission", PropertyType::String, SerializeVec3(config.emission)});
            properties.push_back({prefix + "Transmission", PropertyType::Float, std::to_string(config.transmission)});
            properties.push_back({prefix + "Ior", PropertyType::Float, std::to_string(config.ior)});
            properties.push_back({prefix + "Thickness", PropertyType::Float, std::to_string(config.thickness)});
            properties.push_back({prefix + "AttenuationColor", PropertyType::String, SerializeVec3(config.attenuationColor)});
            properties.push_back({prefix + "AttenuationDistance", PropertyType::Float, std::to_string(config.attenuationDistance)});
            properties.push_back({prefix + "FlipNormalY", PropertyType::Bool, config.flipNormalY ? "true" : "false"});
            properties.push_back({prefix + "LightmapPath", PropertyType::String, config.lightmapTexture ? config.lightmapTexture->GetFilePath() : std::string{}});
        }

        void DeserializeInlineMaterialField(SerializedMaterialData &serializedMaterial, const std::string &fieldName, const std::string &value)
        {
            if (fieldName == "MaterialAsset")
            {
                serializedMaterial.materialAsset = value;
            }
            else if (fieldName == "AlbedoPath")
            {
                serializedMaterial.albedoPath = value;
            }
            else if (fieldName == "Color")
            {
                serializedMaterial.color = ParseVec4(value);
            }
            else if (fieldName == "SurfaceType")
            {
                serializedMaterial.surfaceType = ParseSurfaceType(value);
            }
            else if (fieldName == "AlphaMode")
            {
                serializedMaterial.alphaMode = value == "Blend" || value == "blend" || value == "2"
                                                   ? render::AlphaMode::Blend
                                                   : value == "Mask" || value == "mask" || value == "1"
                                                         ? render::AlphaMode::Mask
                                                         : render::AlphaMode::Opaque;
            }
            else if (fieldName == "AlphaCutoff")
            {
                serializedMaterial.alphaCutoff = std::stof(value);
            }
            else if (fieldName == "CastsShadow")
            {
                serializedMaterial.castsShadow = (value == "true" || value == "1");
            }
            else if (fieldName == "UvScale")
            {
                serializedMaterial.uvScale = ParseVec2(value);
            }
            else if (fieldName == "Metallic")
            {
                serializedMaterial.metallic = std::stof(value);
            }
            else if (fieldName == "Roughness")
            {
                serializedMaterial.roughness = std::stof(value);
            }
            else if (fieldName == "Emission")
            {
                serializedMaterial.emission = ParseVec3(value);
            }
            else if (fieldName == "Transmission")
            {
                serializedMaterial.transmission = std::stof(value);
            }
            else if (fieldName == "Ior")
            {
                serializedMaterial.ior = std::stof(value);
            }
            else if (fieldName == "Thickness")
            {
                serializedMaterial.thickness = std::stof(value);
            }
            else if (fieldName == "AttenuationColor")
            {
                serializedMaterial.attenuationColor = ParseVec3(value);
            }
            else if (fieldName == "AttenuationDistance")
            {
                serializedMaterial.attenuationDistance = std::stof(value);
            }
            else if (fieldName == "FlipNormalY")
            {
                serializedMaterial.flipNormalY = (value == "true");
            }
            else if (fieldName == "LightmapPath")
            {
                serializedMaterial.lightmapPath = value;
            }
        }

        void ApplySerializedMaterialData(render::Material &material, const SerializedMaterialData &serializedMaterial)
        {
            if (serializedMaterial.albedoPath.has_value())
            {
                material.SetAlbedoTexture(serializedMaterial.albedoPath->empty()
                                              ? nullptr
                                              : render::Texture::LoadFromFile(serializedMaterial.albedoPath->c_str()));
            }
            if (serializedMaterial.color.has_value())
            {
                material.SetColor(*serializedMaterial.color);
            }
            if (serializedMaterial.surfaceType.has_value())
            {
                material.SetSurfaceType(*serializedMaterial.surfaceType);
            }
            if (serializedMaterial.alphaMode.has_value())
            {
                material.SetAlphaMode(*serializedMaterial.alphaMode);
            }
            if (serializedMaterial.alphaCutoff.has_value())
            {
                material.SetAlphaCutoff(*serializedMaterial.alphaCutoff);
            }
            if (serializedMaterial.castsShadow.has_value())
            {
                material.SetCastsShadow(*serializedMaterial.castsShadow);
            }
            if (serializedMaterial.uvScale.has_value())
            {
                material.SetUvScale(*serializedMaterial.uvScale);
            }
            if (serializedMaterial.metallic.has_value())
            {
                material.SetMetallic(*serializedMaterial.metallic);
            }
            if (serializedMaterial.roughness.has_value())
            {
                material.SetRoughness(*serializedMaterial.roughness);
            }
            if (serializedMaterial.emission.has_value())
            {
                material.SetEmission(*serializedMaterial.emission);
            }
            if (serializedMaterial.transmission.has_value())
            {
                material.SetTransmission(*serializedMaterial.transmission);
            }
            if (serializedMaterial.ior.has_value())
            {
                material.SetIor(*serializedMaterial.ior);
            }
            if (serializedMaterial.thickness.has_value())
            {
                material.SetThickness(*serializedMaterial.thickness);
            }
            if (serializedMaterial.attenuationColor.has_value())
            {
                material.SetAttenuationColor(*serializedMaterial.attenuationColor);
            }
            if (serializedMaterial.attenuationDistance.has_value())
            {
                material.SetAttenuationDistance(*serializedMaterial.attenuationDistance);
            }
            if (serializedMaterial.flipNormalY.has_value())
            {
                material.SetFlipNormalY(*serializedMaterial.flipNormalY);
            }
            if (serializedMaterial.lightmapPath.has_value())
            {
                if (serializedMaterial.lightmapPath->empty())
                {
                    material.SetLightmapTexture(nullptr);
                }
                else
                {
                    auto *lightmapTexture = core::Engine::GetInstance().GetTextureManager().LoadLightmapFromFile(serializedMaterial.lightmapPath->c_str());
                    material.SetLightmapTexture(lightmapTexture);
                }
            }
        }
    }

    void MeshComponent::SetMesh(render::Mesh *mesh)
    {
        if (m_mesh == mesh)
        {
            return;
        }

        m_mesh = mesh;
        RefreshMeshDerivedState();
        MarkRenderCommandsDirty();

        if (auto *owner = GetOwner())
        {
            if (auto *scene = owner->GetScene())
            {
                scene->MarkShadowLightsDirty();
            }
        }
    }

    void MeshComponent::MarkRenderCommandsDirty()
    {
        m_renderCommandCacheDirty = true;
        m_hasCachedRenderCommandModel = false;
        m_cachedRenderCommands.clear();
    }

    void MeshComponent::RefreshMeshDerivedState()
    {
        m_hasAnimatedNodeSubmeshes = false;
        if (!m_mesh)
        {
            return;
        }

        for (size_t submeshIndex = 0; submeshIndex < m_mesh->GetSubmeshCount(); ++submeshIndex)
        {
            if (m_mesh->GetSubmesh(submeshIndex).animatedNodeIndex >= 0)
            {
                m_hasAnimatedNodeSubmeshes = true;
                break;
            }
        }
    }

    void MeshComponent::UpdateCachedPreviousModels(const glm::mat4 &modelMatrix)
    {
        for (auto &command : m_cachedRenderCommands)
        {
            command.previousModel = modelMatrix;
            command.previousWorldBounds = command.worldBounds;
        }
    }

    render::Material *MeshComponent::CreateUniqueMaterialForMaterialSlot(size_t materialSlotIndex)
    {
        auto *sourceMaterial = GetMaterialForMaterialSlot(materialSlotIndex);
        auto *uniqueMaterial = sourceMaterial ? new render::Material(sourceMaterial->GetConfig()) : new render::Material();
        SetMaterialForMaterialSlot(materialSlotIndex, uniqueMaterial);
        return uniqueMaterial;
    }

    render::Material *MeshComponent::CreateUniqueMaterialForSubmesh(size_t submeshIndex)
    {
        auto *sourceMaterial = GetMaterialForSubmesh(submeshIndex);
        auto *uniqueMaterial = sourceMaterial ? new render::Material(sourceMaterial->GetConfig()) : new render::Material();
        SetMaterialForSubmesh(submeshIndex, uniqueMaterial);
        return uniqueMaterial;
    }

    bool MeshComponent::CreateSkeletonAttachmentEntities()
    {
        auto *owner = GetOwner();
        auto *scene = owner ? owner->GetScene() : nullptr;
        if (!owner || !scene || !m_mesh || !m_mesh->HasSkeleton() || m_submeshIndex >= 0)
        {
            return false;
        }

        const auto &skeleton = m_mesh->GetSkeleton();
        if (skeleton.joints.empty())
        {
            return false;
        }

        bool hasExistingAttachmentChildren = false;
        for (auto *child : owner->GetChildren())
        {
            if (child && child->GetComponent<SkeletonAttachmentComponent>())
            {
                hasExistingAttachmentChildren = true;
                break;
            }
        }
        if (hasExistingAttachmentChildren)
        {
            return false;
        }

        std::vector<Entity *> jointEntities(skeleton.joints.size(), nullptr);
        for (size_t jointIndex = 0; jointIndex < skeleton.joints.size(); ++jointIndex)
        {
            const auto &joint = skeleton.joints[jointIndex];
            auto child = std::make_unique<Entity>(EntityConfig{
                .name = BuildSkeletonAttachmentEntityName(joint, jointIndex),
                .tags = {"SkeletonAttachment"},
            });
            auto *childPtr = child.get();
            childPtr->CreateComponent<SkeletonAttachmentComponent>(joint.nodeIndex, joint.name);
            jointEntities[jointIndex] = scene->AddEntity(std::move(child), owner);
        }

        for (size_t jointIndex = 0; jointIndex < skeleton.joints.size(); ++jointIndex)
        {
            const auto &joint = skeleton.joints[jointIndex];
            if (joint.parentJointIndex < 0 || joint.parentJointIndex >= static_cast<int>(jointEntities.size()))
            {
                continue;
            }

            auto *jointEntity = jointEntities[jointIndex];
            auto *parentJointEntity = jointEntities[static_cast<size_t>(joint.parentJointIndex)];
            if (jointEntity && parentJointEntity)
            {
                jointEntity->SetParent(parentJointEntity);
            }
        }

        return true;
    }

    std::vector<Property> MeshComponent::Serialize() const
    {
        std::vector<Property> properties{
            {"Static", PropertyType::Bool, m_isStatic ? "true" : "false"},
            {"Visible", PropertyType::Bool, m_visible ? "true" : "false"},
            {"SubmeshIndex", PropertyType::Int, std::to_string(m_submeshIndex)},
            {"SubmeshCount", PropertyType::Int, std::to_string(m_submeshCount)},
            {"ModelAssetId", PropertyType::String, m_modelAssetId},
            {"ModelObjectId", PropertyType::String, std::to_string(m_modelObjectId)},
            {"SourceMeshPath", PropertyType::String, m_sourceMeshPath},
            {"UseGeneratedLods", PropertyType::Bool, m_useGeneratedLods ? "true" : "false"},
            {"MeshPositionOffset", PropertyType::Vec3, SerializeVec3(m_meshPositionOffset)},
            {"MeshRotationOffset", PropertyType::Vec3, SerializeVec3(m_meshRotationOffset)},
            {"MaterialSlotCount", PropertyType::Int, std::to_string(m_materials.size())},
        };

        for (size_t materialSlotIndex = 0; materialSlotIndex < m_materials.size(); ++materialSlotIndex)
        {
            auto *material = GetMaterialForMaterialSlot(materialSlotIndex);
            if (!material)
            {
                continue;
            }

            const auto &config = material->GetConfig();
            const std::string prefix = std::string(kMaterialSlotPrefix) + std::to_string(materialSlotIndex) + ".";
            const auto &materialAssetReference = GetMaterialAssetForMaterialSlot(materialSlotIndex);
            if (!materialAssetReference.empty())
            {
                properties.push_back({prefix + "MaterialAsset", PropertyType::String, materialAssetReference});
                continue;
            }
            SerializeInlineMaterialProperties(properties, prefix, config);
        }

        for (size_t submeshIndex = 0; submeshIndex < m_submeshMaterials.size(); ++submeshIndex)
        {
            auto *material = m_submeshMaterials[submeshIndex];
            if (!material)
            {
                continue;
            }

            const auto &config = material->GetConfig();
            const std::string prefix = std::string(kSubmeshOverridePrefix) + std::to_string(submeshIndex) + ".";
            const auto &materialAssetReference = GetMaterialAssetForSubmesh(submeshIndex);
            if (!materialAssetReference.empty())
            {
                properties.push_back({prefix + "MaterialAsset", PropertyType::String, materialAssetReference});
                continue;
            }
            SerializeInlineMaterialProperties(properties, prefix, config);
        }

        return properties;
    }

    void MeshComponent::Deserialize(const std::vector<Property> &properties)
    {
        std::map<size_t, SerializedMaterialData> serializedMaterials;
        std::map<size_t, SerializedMaterialData> serializedSubmeshMaterials;
        std::string sourceMeshPath;
        std::string modelAssetId;
        std::uint64_t modelObjectId = 0;
        bool useGeneratedLods = m_useGeneratedLods;

        for (const auto &property : properties)
        {
            if (property.name == "Static")
            {
                m_isStatic = (property.value == "true");
            }
            else if (property.name == "Visible")
            {
                m_visible = (property.value == "true");
            }
            else if (property.name == "SubmeshIndex")
            {
                m_submeshIndex = std::stoi(property.value);
            }
            else if (property.name == "SubmeshCount")
            {
                m_submeshCount = std::max(1, std::stoi(property.value));
            }
            else if (property.name == "ModelAssetId")
            {
                modelAssetId = property.value;
            }
            else if (property.name == "ModelObjectId")
            {
                try { modelObjectId = std::stoull(property.value); } catch (...) { modelObjectId = 0; }
            }
            else if (property.name == "SourceMeshPath")
            {
                sourceMeshPath = property.value;
            }
            else if (property.name == "UseGeneratedLods")
            {
                useGeneratedLods = property.value == "true" || property.value == "1";
            }
            else if (property.name == "MeshPositionOffset")
            {
                m_meshPositionOffset = ParseVec3(property.value, glm::vec3(0.0f));
            }
            else if (property.name == "MeshRotationOffset")
            {
                m_meshRotationOffset = ParseVec3(property.value, glm::vec3(0.0f));
            }
            else if (property.name.rfind(kMaterialSlotPrefix, 0) == 0)
            {
                const std::string remainder = property.name.substr(std::char_traits<char>::length(kMaterialSlotPrefix));
                const auto separatorIndex = remainder.find('.');
                if (separatorIndex == std::string::npos)
                {
                    continue;
                }

                const size_t materialSlotIndex = static_cast<size_t>(std::stoul(remainder.substr(0, separatorIndex)));
                const std::string fieldName = remainder.substr(separatorIndex + 1);
                auto &serializedMaterial = serializedMaterials[materialSlotIndex];
                DeserializeInlineMaterialField(serializedMaterial, fieldName, property.value);
            }
            else if (property.name.rfind(kSubmeshOverridePrefix, 0) == 0)
            {
                const std::string remainder = property.name.substr(std::char_traits<char>::length(kSubmeshOverridePrefix));
                const auto separatorIndex = remainder.find('.');
                if (separatorIndex == std::string::npos)
                {
                    continue;
                }

                const size_t submeshIndex = static_cast<size_t>(std::stoul(remainder.substr(0, separatorIndex)));
                const std::string fieldName = remainder.substr(separatorIndex + 1);
                auto &serializedMaterial = serializedSubmeshMaterials[submeshIndex];
                DeserializeInlineMaterialField(serializedMaterial, fieldName, property.value);
            }
        }

        if (!modelAssetId.empty())
        {
            auto &engine = core::Engine::GetInstance();
            sourceMeshPath = assets::Project::IsEngineAssetReference(modelAssetId)
                                 ? modelAssetId
                                 : engine.GetAssetManager().ResolveModelObject(modelAssetId, modelObjectId);
            if (sourceMeshPath.empty()) return;
            if (auto *builtinMesh = engine.GetAssetManager().LoadMeshAsset(sourceMeshPath))
            {
                SetMesh(builtinMesh);
                const auto &materialReferences = engine.GetAssetManager().GetMeshAssetMaterialReferences(sourceMeshPath);
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
                SetMaterials(loadedMaterials);
                for (size_t materialSlotIndex = 0; materialSlotIndex < materialReferences.size(); ++materialSlotIndex)
                {
                    SetMaterialAssetForMaterialSlot(materialSlotIndex, materialReferences[materialSlotIndex]);
                }
                m_sourceMeshPath = sourceMeshPath;
                m_modelAssetId = modelAssetId;
                m_modelObjectId = modelObjectId;
                m_useGeneratedLods = false;
            }
            else
            {
                const std::string resolvedMeshPath = engine.GetAssetManager().ResolveMeshAssetSourcePath(sourceMeshPath);
                auto importedMeshAsset = useGeneratedLods ? engine.GenerateMeshAssetLods(resolvedMeshPath) : engine.ImportMeshAsset(resolvedMeshPath);
                if (importedMeshAsset.mesh)
                {
                    SetMesh(importedMeshAsset.mesh);
                    SetMaterials(importedMeshAsset.materials);
                    m_sourceMeshPath = sourceMeshPath;
                    m_modelAssetId = modelAssetId;
                    m_modelObjectId = modelObjectId;
                    m_useGeneratedLods = useGeneratedLods;
                    if (importedMeshAsset.animations && !importedMeshAsset.animations->empty())
                    {
                        if (auto *owner = GetOwner())
                        {
                            if (auto *animationComponent = owner->GetComponent<AnimationComponent>())
                            {
                                animationComponent->SetClipsFromImportedAnimations(*importedMeshAsset.animations);
                                animationComponent->SetSourceAnimationPath(sourceMeshPath);
                            }
                        }
                    }
                }
            }
        }
        else if (!sourceMeshPath.empty())
        {
            auto &engine = core::Engine::GetInstance();
            const std::string resolvedMeshPath = engine.GetAssetManager().ResolveMeshAssetSourcePath(sourceMeshPath);
            auto importedMeshAsset = useGeneratedLods ? engine.GenerateMeshAssetLods(resolvedMeshPath) : engine.ImportMeshAsset(resolvedMeshPath);
            if (importedMeshAsset.mesh)
            {
                SetMesh(importedMeshAsset.mesh);
                SetMaterials(importedMeshAsset.materials);
                m_sourceMeshPath = sourceMeshPath;
                m_useGeneratedLods = useGeneratedLods;
                if (importedMeshAsset.animations && !importedMeshAsset.animations->empty())
                {
                    if (auto *owner = GetOwner())
                    {
                        auto *animationComponent = owner->GetComponent<AnimationComponent>();
                        if (!animationComponent) animationComponent = owner->CreateComponent<AnimationComponent>();
                        animationComponent->SetClipsFromImportedAnimations(*importedMeshAsset.animations);
                        animationComponent->SetSourceAnimationPath(sourceMeshPath);
                    }
                }
            }
        }

        for (const auto &[materialSlotIndex, serializedMaterial] : serializedMaterials)
        {
            auto *material = CreateUniqueMaterialForMaterialSlot(materialSlotIndex);
            if (!material)
            {
                continue;
            }

            if (serializedMaterial.materialAsset.has_value())
            {
                if (auto *materialAsset = core::Engine::GetInstance().GetAssetManager().LoadMaterialAsset(*serializedMaterial.materialAsset))
                {
                    SetMaterialForMaterialSlot(materialSlotIndex, materialAsset);
                    SetMaterialAssetForMaterialSlot(materialSlotIndex, *serializedMaterial.materialAsset);
                    material = materialAsset;
                }
            }

            ApplySerializedMaterialData(*material, serializedMaterial);
        }

        for (const auto &[submeshIndex, serializedMaterial] : serializedSubmeshMaterials)
        {
            auto *material = CreateUniqueMaterialForSubmesh(submeshIndex);
            if (!material)
            {
                continue;
            }

            if (serializedMaterial.materialAsset.has_value())
            {
                if (auto *materialAsset = core::Engine::GetInstance().GetAssetManager().LoadMaterialAsset(*serializedMaterial.materialAsset))
                {
                    SetMaterialForSubmesh(submeshIndex, materialAsset);
                    SetMaterialAssetForSubmesh(submeshIndex, *serializedMaterial.materialAsset);
                    material = materialAsset;
                }
            }

            ApplySerializedMaterialData(*material, serializedMaterial);
        }
    }

    void MeshComponent::Update(float deltaTime)
    {
        (void)deltaTime;
    }

    MeshComponent *MeshComponent::FindMeshOffsetSource() const
    {
        if (m_submeshIndex < 0)
        {
            return const_cast<MeshComponent *>(this);
        }

        auto *owner = GetOwner();
        for (auto *current = owner ? owner->GetParent() : nullptr; current != nullptr; current = current->GetParent())
        {
            auto *candidate = current->GetComponent<MeshComponent>();
            if (!candidate || candidate->GetSubmeshIndex() >= 0)
            {
                continue;
            }

            const bool sameMesh = candidate->GetMesh() && candidate->GetMesh() == m_mesh;
            const bool sameAsset = !m_sourceMeshPath.empty() && candidate->GetSourceMeshPath() == m_sourceMeshPath;
            if (sameMesh || sameAsset)
            {
                return candidate;
            }
        }

        return const_cast<MeshComponent *>(this);
    }

    void MeshComponent::SetMeshPositionOffset(const glm::vec3 &offset)
    {
        auto *source = FindMeshOffsetSource();
        if (source != this)
        {
            source->SetMeshPositionOffset(offset);
            return;
        }
        if (m_meshPositionOffset == offset)
        {
            return;
        }
        m_meshPositionOffset = offset;
        MarkRenderCommandsDirty();
    }

    const glm::vec3 &MeshComponent::GetMeshPositionOffset() const
    {
        return FindMeshOffsetSource()->m_meshPositionOffset;
    }

    void MeshComponent::SetMeshRotationOffset(const glm::vec3 &offset)
    {
        auto *source = FindMeshOffsetSource();
        if (source != this)
        {
            source->SetMeshRotationOffset(offset);
            return;
        }
        if (m_meshRotationOffset == offset)
        {
            return;
        }
        m_meshRotationOffset = offset;
        MarkRenderCommandsDirty();
    }

    const glm::vec3 &MeshComponent::GetMeshRotationOffset() const
    {
        return FindMeshOffsetSource()->m_meshRotationOffset;
    }

    glm::mat4 MeshComponent::GetMeshOffsetTransform() const
    {
        const auto *source = FindMeshOffsetSource();
        glm::mat4 transform(1.0f);
        transform = glm::translate(transform, source->m_meshPositionOffset);
        transform = glm::rotate(transform, glm::radians(source->m_meshRotationOffset.x), glm::vec3(1.0f, 0.0f, 0.0f));
        transform = glm::rotate(transform, glm::radians(source->m_meshRotationOffset.y), glm::vec3(0.0f, 1.0f, 0.0f));
        transform = glm::rotate(transform, glm::radians(source->m_meshRotationOffset.z), glm::vec3(0.0f, 0.0f, 1.0f));
        return transform;
    }

    void MeshComponent::SubmitRenderCommands()
    {
        if (m_mesh && m_visible)
        {
            auto entity = GetOwner();
            const glm::mat4 modelMatrix = entity->GetWorldTransform() * GetMeshOffsetTransform();

            AnimationComponent *animationComponent = (m_mesh->HasSkeleton() || m_hasAnimatedNodeSubmeshes)
                                                         ? FindAnimationComponent(entity)
                                                         : nullptr;
            const std::vector<glm::mat4> *jointMatrices = nullptr;
            bool skinningPoseChanged = false;
            if (m_mesh->HasSkeleton())
            {
                if (animationComponent)
                {
                    // Capture this before GetJointMatrices evaluates the pose
                    // and clears the animation component's dirty flag.
                    skinningPoseChanged = animationComponent->IsJointPoseDirty();
                    jointMatrices = &animationComponent->GetJointMatrices(m_mesh->GetSkeleton(), m_mesh->GetAnimationNodes());
                }
            }
            // Command construction is independent of the Static rendering flag.
            // Any unskinned mesh with no active node animation can reuse its
            // commands while its world transform remains unchanged.
            const bool canCacheRenderCommands = !jointMatrices &&
                                                (!m_hasAnimatedNodeSubmeshes || !animationComponent || animationComponent->GetClipCount() == 0);

            auto &renderer = PlutoGE::core::Engine::GetInstance().GetRenderer();
            if (canCacheRenderCommands &&
                !m_renderCommandCacheDirty &&
                m_hasCachedRenderCommandModel &&
                AreMatricesApproximatelyEqual(m_cachedRenderCommandModel, modelMatrix))
            {
                renderer.SubmitSortedRenderCommands(m_cachedRenderCommands, true);

                m_previousModelMatrix = modelMatrix;
                m_hasPreviousModelMatrix = true;
                return;
            }

            const size_t meshSubmeshCount = std::max<size_t>(m_mesh->GetSubmeshCount(), 1);
            const size_t submeshBegin = m_submeshIndex >= 0 ? static_cast<size_t>(m_submeshIndex) : 0;
            const size_t submeshEnd = m_submeshIndex >= 0 ? std::min(submeshBegin + static_cast<size_t>(std::max(1, m_submeshCount)), meshSubmeshCount) : meshSubmeshCount;
            std::vector<render::RenderCommand> rebuiltCommands;
            if (canCacheRenderCommands)
            {
                rebuiltCommands.reserve(submeshEnd - submeshBegin);
            }

            for (size_t submeshIndex = submeshBegin; submeshIndex < submeshEnd; ++submeshIndex)
            {
                auto *material = GetMaterialForSubmesh(submeshIndex);
                if (!material)
                {
                    continue;
                }

                const auto &submesh = submeshIndex < m_mesh->GetSubmeshCount() ? m_mesh->GetSubmesh(submeshIndex) : render::Submesh{};
                glm::mat4 submeshModelMatrix = modelMatrix;
                if (!jointMatrices && submesh.animatedNodeIndex >= 0)
                {
                    submeshModelMatrix = modelMatrix * (animationComponent && animationComponent->GetClipCount() > 0
                                                            ? animationComponent->GetNodeMatrix(m_mesh->GetAnimationNodes(), submesh.animatedNodeIndex)
                                                            : ComputeAnimationNodeBindMatrix(m_mesh->GetAnimationNodes(), submesh.animatedNodeIndex));
                }

                render::RenderCommand command;
                command.model = submeshModelMatrix;
                command.previousModel = m_hasPreviousModelMatrix ? m_previousModelMatrix : submeshModelMatrix;
                command.material = material;
                command.mesh = m_mesh;
                command.shader = material->GetShader();
                command.worldBounds = ComputeWorldBounds(*m_mesh, submeshIndex, submeshModelMatrix);
                command.previousWorldBounds = ComputeWorldBounds(*m_mesh, submeshIndex, command.previousModel);
                command.jointMatrices = jointMatrices;
                command.skinningPoseChanged = skinningPoseChanged;
                command.submeshIndex = static_cast<uint32_t>(submeshIndex);
                command.isStatic = m_isStatic;
                command.usePrimaryUvForLightmap = !m_mesh->HasUsableLightmapUvsForSubmesh(submeshIndex);

                renderer.SubmitRenderCommand(command);
                if (canCacheRenderCommands)
                {
                    rebuiltCommands.push_back(command);
                }
            }

            if (canCacheRenderCommands)
            {
                std::sort(rebuiltCommands.begin(), rebuiltCommands.end(), CompareRenderCommandKeys);
                m_cachedRenderCommands = std::move(rebuiltCommands);
                m_cachedRenderCommandModel = modelMatrix;
                m_hasCachedRenderCommandModel = true;
                m_renderCommandCacheDirty = false;
                UpdateCachedPreviousModels(modelMatrix);
            }

            m_previousModelMatrix = modelMatrix;
            m_hasPreviousModelMatrix = true;
        };
    }
}
