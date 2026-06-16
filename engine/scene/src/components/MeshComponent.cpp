#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/AnimationComponent.h"

#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Texture.h"

#include "PlutoGE/core/Engine.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <map>
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

        std::string SerializeVec4(const glm::vec4 &value)
        {
            return std::to_string(value.r) + "," + std::to_string(value.g) + "," + std::to_string(value.b) + "," + std::to_string(value.a);
        }

        glm::vec4 ParseVec4(const std::string &value, const glm::vec4 &fallback = glm::vec4(1.0f))
        {
            glm::vec4 parsedValue = fallback;
            std::sscanf(value.c_str(), "%f,%f,%f,%f", &parsedValue.r, &parsedValue.g, &parsedValue.b, &parsedValue.a);
            return parsedValue;
        }

        struct SerializedMaterialData
        {
            std::optional<std::string> materialAsset;
            std::optional<glm::vec4> color;
            std::optional<float> metallic;
            std::optional<float> roughness;
            std::optional<bool> flipNormalY;
            std::optional<std::string> lightmapPath;
        };
    }

    void MeshComponent::SetMesh(render::Mesh *mesh)
    {
        if (m_mesh == mesh)
        {
            return;
        }

        m_mesh = mesh;
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

    std::vector<Property> MeshComponent::Serialize() const
    {
        std::vector<Property> properties{
            {"Static", PropertyType::Bool, m_isStatic ? "true" : "false"},
            {"SourceMesh", PropertyType::String, m_sourceMeshPath},
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
            properties.push_back({prefix + "Color", PropertyType::String, SerializeVec4(config.color)});
            properties.push_back({prefix + "Metallic", PropertyType::Float, std::to_string(config.metallic)});
            properties.push_back({prefix + "Roughness", PropertyType::Float, std::to_string(config.roughness)});
            properties.push_back({prefix + "FlipNormalY", PropertyType::Bool, config.flipNormalY ? "true" : "false"});
            properties.push_back({prefix + "LightmapPath", PropertyType::String, config.lightmapTexture ? config.lightmapTexture->GetFilePath() : std::string{}});
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
            properties.push_back({prefix + "Color", PropertyType::String, SerializeVec4(config.color)});
            properties.push_back({prefix + "Metallic", PropertyType::Float, std::to_string(config.metallic)});
            properties.push_back({prefix + "Roughness", PropertyType::Float, std::to_string(config.roughness)});
            properties.push_back({prefix + "FlipNormalY", PropertyType::Bool, config.flipNormalY ? "true" : "false"});
            properties.push_back({prefix + "LightmapPath", PropertyType::String, config.lightmapTexture ? config.lightmapTexture->GetFilePath() : std::string{}});
        }

        return properties;
    }

    void MeshComponent::Deserialize(const std::vector<Property> &properties)
    {
        std::map<size_t, SerializedMaterialData> serializedMaterials;
        std::map<size_t, SerializedMaterialData> serializedSubmeshMaterials;
        std::string sourceMeshPath = m_sourceMeshPath;

        for (const auto &property : properties)
        {
            if (property.name == "Static")
            {
                m_isStatic = (property.value == "true");
            }
            else if (property.name == "SourceMesh")
            {
                sourceMeshPath = property.value;
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
                if (fieldName == "MaterialAsset")
                {
                    serializedMaterial.materialAsset = property.value;
                }
                else if (fieldName == "Color")
                {
                    serializedMaterial.color = ParseVec4(property.value);
                }
                else if (fieldName == "Metallic")
                {
                    serializedMaterial.metallic = std::stof(property.value);
                }
                else if (fieldName == "Roughness")
                {
                    serializedMaterial.roughness = std::stof(property.value);
                }
                else if (fieldName == "FlipNormalY")
                {
                    serializedMaterial.flipNormalY = (property.value == "true");
                }
                else if (fieldName == "LightmapPath")
                {
                    serializedMaterial.lightmapPath = property.value;
                }
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
                if (fieldName == "MaterialAsset")
                {
                    serializedMaterial.materialAsset = property.value;
                }
                else if (fieldName == "Color")
                {
                    serializedMaterial.color = ParseVec4(property.value);
                }
                else if (fieldName == "Metallic")
                {
                    serializedMaterial.metallic = std::stof(property.value);
                }
                else if (fieldName == "Roughness")
                {
                    serializedMaterial.roughness = std::stof(property.value);
                }
                else if (fieldName == "FlipNormalY")
                {
                    serializedMaterial.flipNormalY = (property.value == "true");
                }
                else if (fieldName == "LightmapPath")
                {
                    serializedMaterial.lightmapPath = property.value;
                }
            }
        }

        if (!sourceMeshPath.empty())
        {
            auto &engine = core::Engine::GetInstance();
            if (auto *builtinMesh = engine.GetAssetManager().LoadMeshAsset(sourceMeshPath))
            {
                SetMesh(builtinMesh);
                SetMaterials({engine.GetAssetManager().LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference))});
                SetMaterialAssetForMaterialSlot(0, std::string(assets::Project::kBuiltinDefaultShadedMaterialReference));
                m_sourceMeshPath = sourceMeshPath;
            }
            else
            {
                const std::string resolvedMeshPath = engine.GetAssetManager().ResolveMeshAssetSourcePath(sourceMeshPath);
                auto importedMeshAsset = engine.ImportMeshAsset(resolvedMeshPath);
                if (importedMeshAsset.mesh)
                {
                    SetMesh(importedMeshAsset.mesh);
                    SetMaterials(importedMeshAsset.materials);
                    m_sourceMeshPath = sourceMeshPath;
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

            if (serializedMaterial.color.has_value())
            {
                material->SetColor(*serializedMaterial.color);
            }
            if (serializedMaterial.metallic.has_value())
            {
                material->SetMetallic(*serializedMaterial.metallic);
            }
            if (serializedMaterial.roughness.has_value())
            {
                material->SetRoughness(*serializedMaterial.roughness);
            }
            if (serializedMaterial.flipNormalY.has_value())
            {
                material->SetFlipNormalY(*serializedMaterial.flipNormalY);
            }
            if (serializedMaterial.lightmapPath.has_value())
            {
                if (serializedMaterial.lightmapPath->empty())
                {
                    material->SetLightmapTexture(nullptr);
                }
                else
                {
                    auto *lightmapTexture = core::Engine::GetInstance().GetTextureManager().LoadLightmapFromFile(serializedMaterial.lightmapPath->c_str());
                    material->SetLightmapTexture(lightmapTexture);
                }
            }
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

            if (serializedMaterial.color.has_value())
            {
                material->SetColor(*serializedMaterial.color);
            }
            if (serializedMaterial.metallic.has_value())
            {
                material->SetMetallic(*serializedMaterial.metallic);
            }
            if (serializedMaterial.roughness.has_value())
            {
                material->SetRoughness(*serializedMaterial.roughness);
            }
            if (serializedMaterial.flipNormalY.has_value())
            {
                material->SetFlipNormalY(*serializedMaterial.flipNormalY);
            }
            if (serializedMaterial.lightmapPath.has_value())
            {
                if (serializedMaterial.lightmapPath->empty())
                {
                    material->SetLightmapTexture(nullptr);
                }
                else
                {
                    auto *lightmapTexture = core::Engine::GetInstance().GetTextureManager().LoadLightmapFromFile(serializedMaterial.lightmapPath->c_str());
                    material->SetLightmapTexture(lightmapTexture);
                }
            }
        }
    }

    void MeshComponent::Update(float deltaTime)
    {
        if (m_mesh)
        {
            auto entity = GetOwner();
            glm::mat4 modelMatrix = entity->GetWorldTransform();

            auto &renderer = PlutoGE::core::Engine::GetInstance().GetRenderer();
            if (m_isStatic &&
                !m_mesh->HasSkeleton() &&
                !m_renderCommandCacheDirty &&
                m_hasCachedRenderCommandModel &&
                AreMatricesApproximatelyEqual(m_cachedRenderCommandModel, modelMatrix))
            {
                for (const auto &command : m_cachedRenderCommands)
                {
                    renderer.SubmitRenderCommand(command);
                }

                m_previousModelMatrix = modelMatrix;
                m_hasPreviousModelMatrix = true;
                return;
            }

            const size_t submeshCount = std::max<size_t>(m_mesh->GetSubmeshCount(), 1);
            const std::vector<glm::mat4> *jointMatrices = nullptr;
            if (m_mesh->HasSkeleton())
            {
                if (auto *animationComponent = entity->GetComponent<AnimationComponent>())
                {
                    jointMatrices = &animationComponent->GetJointMatrices(m_mesh->GetSkeleton());
                }
            }
            std::vector<render::RenderCommand> rebuiltCommands;
            if (m_isStatic && !jointMatrices)
            {
                rebuiltCommands.reserve(submeshCount);
            }

            for (size_t submeshIndex = 0; submeshIndex < submeshCount; ++submeshIndex)
            {
                auto *material = GetMaterialForSubmesh(submeshIndex);
                if (!material)
                {
                    continue;
                }

                render::RenderCommand command;
                command.model = modelMatrix;
                command.previousModel = m_hasPreviousModelMatrix ? m_previousModelMatrix : modelMatrix;
                command.material = material;
                command.mesh = m_mesh;
                command.shader = material->GetShader();
                command.worldBounds = ComputeWorldBounds(*m_mesh, submeshIndex, modelMatrix);
                command.previousWorldBounds = ComputeWorldBounds(*m_mesh, submeshIndex, command.previousModel);
                command.jointMatrices = jointMatrices;
                command.submeshIndex = static_cast<uint32_t>(submeshIndex);
                command.isStatic = m_isStatic;
                command.usePrimaryUvForLightmap = !m_mesh->HasUsableLightmapUvsForSubmesh(submeshIndex);

                renderer.SubmitRenderCommand(command);
                if (m_isStatic && !jointMatrices)
                {
                    rebuiltCommands.push_back(command);
                }
            }

            if (m_isStatic && !jointMatrices)
            {
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
