#include "PlutoGE/scene/SceneSerializer.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/render/Camera.h"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace PlutoGE::scene
{
    namespace
    {
        std::string EscapeText(std::string_view text)
        {
            std::string escaped;
            escaped.reserve(text.size());

            for (const char character : text)
            {
                switch (character)
                {
                case '\\':
                    escaped += "\\\\";
                    break;
                case '\n':
                    escaped += "\\n";
                    break;
                case '\t':
                    escaped += "\\t";
                    break;
                default:
                    escaped.push_back(character);
                    break;
                }
            }

            return escaped;
        }

        std::vector<std::string> SplitEscaped(std::string_view text, char delimiter)
        {
            std::vector<std::string> parts;
            std::string current;
            bool escaping = false;

            for (const char character : text)
            {
                if (escaping)
                {
                    switch (character)
                    {
                    case 'n':
                        current.push_back('\n');
                        break;
                    case 't':
                        current.push_back('\t');
                        break;
                    case '\\':
                        current.push_back('\\');
                        break;
                    default:
                        current.push_back(character);
                        break;
                    }

                    escaping = false;
                    continue;
                }

                if (character == '\\')
                {
                    escaping = true;
                    continue;
                }

                if (character == delimiter)
                {
                    parts.push_back(current);
                    current.clear();
                    continue;
                }

                current.push_back(character);
            }

            parts.push_back(current);
            return parts;
        }

        std::string SerializeVec3(const glm::vec3 &value)
        {
            return std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z);
        }

        glm::vec3 ParseVec3(std::string_view value)
        {
            glm::vec3 parsedValue{0.0f};
            std::sscanf(std::string(value).c_str(), "%f,%f,%f", &parsedValue.x, &parsedValue.y, &parsedValue.z);
            return parsedValue;
        }

        glm::ivec3 ParseIVec3(std::string_view value)
        {
            glm::ivec3 parsedValue{0};
            std::sscanf(std::string(value).c_str(), "%d,%d,%d", &parsedValue.x, &parsedValue.y, &parsedValue.z);
            return parsedValue;
        }

        std::string SerializeIVec3(const glm::ivec3 &value)
        {
            return std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z);
        }

        void CollectEntitiesRecursive(const Entity *entity, std::vector<const Entity *> &entities)
        {
            if (!entity)
            {
                return;
            }

            entities.push_back(entity);
            for (auto *child : entity->GetChildren())
            {
                CollectEntitiesRecursive(child, entities);
            }
        }

        std::string ResolveComponentTypeName(const Component &component)
        {
            if (dynamic_cast<const MeshComponent *>(&component))
            {
                return "MeshComponent";
            }
            if (dynamic_cast<const CameraComponent *>(&component))
            {
                return "CameraComponent";
            }
            if (dynamic_cast<const LightComponent *>(&component))
            {
                return "LightComponent";
            }

            return {};
        }

        std::unique_ptr<Component> CreateComponentForType(std::string_view componentType)
        {
            if (componentType == "MeshComponent")
            {
                return std::make_unique<MeshComponent>(MeshComponentConfig{});
            }
            if (componentType == "CameraComponent")
            {
                return std::make_unique<CameraComponent>(new render::Camera(render::CameraConfig{}));
            }
            if (componentType == "LightComponent")
            {
                return std::make_unique<LightComponent>();
            }

            return nullptr;
        }
    }

    bool SceneSerializer::Save(const Scene &scene, const std::string &filePath, std::string *errorMessage)
    {
        std::ofstream output(filePath, std::ios::out | std::ios::trunc);
        if (!output.is_open())
        {
            if (errorMessage)
            {
                *errorMessage = "Failed to open scene file for writing.";
            }
            return false;
        }

        output << "SCENE\t1\n";

        if (!scene.GetEnvironmentMapPath().empty())
        {
            output << "ENVIRONMENT\t"
                   << EscapeText(scene.GetEnvironmentMapPath()) << '\t'
                   << scene.GetEnvironmentIntensity() << '\n';
        }

        const auto &probeVolume = scene.GetBakedProbeVolume();
        if (probeVolume.IsValid())
        {
            output << "PROBE\t"
                   << SerializeVec3(probeVolume.origin) << '\t'
                   << SerializeVec3(probeVolume.size) << '\t'
                   << SerializeIVec3(probeVolume.resolution) << '\n';
            for (const auto &sample : probeVolume.irradiance)
            {
                output << "PROBE_SAMPLE\t" << SerializeVec3(sample) << '\n';
            }
        }

        std::vector<const Entity *> entities;
        for (auto *rootEntity : scene.GetRootEntities())
        {
            CollectEntitiesRecursive(rootEntity, entities);
        }

        for (const auto *entity : entities)
        {
            output << "ENTITY\t"
                   << entity->GetID() << '\t'
                   << (entity->GetParent() ? entity->GetParent()->GetID() : 0) << '\t'
                   << (entity->IsActive() ? 1 : 0) << '\t'
                   << EscapeText(entity->GetName()) << '\t'
                   << SerializeVec3(entity->GetPosition()) << '\t'
                   << SerializeVec3(entity->GetRotation()) << '\t'
                   << SerializeVec3(entity->GetScale()) << '\n';

            for (const auto &bucket : entity->GetComponentBuckets())
            {
                for (const auto *component : bucket)
                {
                    if (!component)
                    {
                        continue;
                    }

                    const auto componentType = ResolveComponentTypeName(*component);
                    if (componentType.empty())
                    {
                        continue;
                    }

                    output << "COMPONENT\t" << entity->GetID() << '\t' << componentType << '\n';
                    for (const auto &property : component->Serialize())
                    {
                        output << "PROPERTY\t"
                               << EscapeText(property.name) << '\t'
                               << static_cast<int>(property.type) << '\t'
                               << EscapeText(property.value) << '\t'
                               << property.enumOptions.size();
                        for (const auto &option : property.enumOptions)
                        {
                            output << '\t' << EscapeText(option);
                        }
                        output << '\n';
                    }
                    output << "END_COMPONENT\n";
                }
            }
        }

        return true;
    }

    std::unique_ptr<Scene> SceneSerializer::Load(const std::string &filePath, std::string *errorMessage)
    {
        std::ifstream input(filePath);
        if (!input.is_open())
        {
            if (errorMessage)
            {
                *errorMessage = "Failed to open scene file for reading.";
            }
            return nullptr;
        }

        auto scene = std::make_unique<Scene>();
        scene->SetFilePath(std::filesystem::absolute(std::filesystem::path(filePath)).lexically_normal().string());

        struct PendingEntityParent
        {
            EntityID id = 0;
            EntityID parentId = 0;
        };

        struct PendingComponent
        {
            EntityID entityId = 0;
            std::string typeName;
            std::vector<Property> properties;
        };

        std::unordered_map<EntityID, Entity *> entityMap;
        std::vector<PendingEntityParent> pendingParents;
        std::optional<PendingComponent> activeComponent;
        BakedProbeVolume bakedProbeVolume;
        std::string environmentMapPath;
        float environmentIntensity = 1.0f;

        std::string line;
        while (std::getline(input, line))
        {
            const auto tokens = SplitEscaped(line, '\t');
            if (tokens.empty())
            {
                continue;
            }

            if (tokens[0] == "SCENE")
            {
                continue;
            }

            if (tokens[0] == "ENVIRONMENT" && tokens.size() >= 3)
            {
                environmentMapPath = tokens[1];
                environmentIntensity = std::stof(tokens[2]);
                continue;
            }

            if (tokens[0] == "PROBE" && tokens.size() >= 4)
            {
                bakedProbeVolume.origin = ParseVec3(tokens[1]);
                bakedProbeVolume.size = ParseVec3(tokens[2]);
                bakedProbeVolume.resolution = ParseIVec3(tokens[3]);
                bakedProbeVolume.irradiance.clear();
                continue;
            }

            if (tokens[0] == "PROBE_SAMPLE" && tokens.size() >= 2)
            {
                bakedProbeVolume.irradiance.push_back(ParseVec3(tokens[1]));
                continue;
            }

            if (tokens[0] == "ENTITY" && tokens.size() >= 8)
            {
                const EntityID serializedId = static_cast<EntityID>(std::stoul(tokens[1]));
                const EntityID parentId = static_cast<EntityID>(std::stoul(tokens[2]));
                const bool isActive = tokens[3] == "1";

                auto entity = std::make_unique<Entity>(EntityConfig{.name = tokens[4]});
                entity->SetPosition(ParseVec3(tokens[5]));
                entity->SetRotation(ParseVec3(tokens[6]));
                entity->SetScale(ParseVec3(tokens[7]));
                entity->SetActive(isActive);

                auto *entityPtr = scene->AddEntity(std::move(entity));
                entityMap.emplace(serializedId, entityPtr);
                pendingParents.push_back(PendingEntityParent{.id = serializedId, .parentId = parentId});
                continue;
            }

            if (tokens[0] == "COMPONENT" && tokens.size() >= 3)
            {
                activeComponent = PendingComponent{
                    .entityId = static_cast<EntityID>(std::stoul(tokens[1])),
                    .typeName = tokens[2],
                };
                continue;
            }

            if (tokens[0] == "PROPERTY" && tokens.size() >= 5 && activeComponent.has_value())
            {
                Property property;
                property.name = tokens[1];
                property.type = static_cast<PropertyType>(std::stoi(tokens[2]));
                property.value = tokens[3];
                const int enumCount = std::stoi(tokens[4]);
                for (int enumIndex = 0; enumIndex < enumCount && 5 + enumIndex < static_cast<int>(tokens.size()); ++enumIndex)
                {
                    property.enumOptions.push_back(tokens[5 + enumIndex]);
                }
                activeComponent->properties.push_back(std::move(property));
                continue;
            }

            if (tokens[0] == "END_COMPONENT" && activeComponent.has_value())
            {
                const auto entityIt = entityMap.find(activeComponent->entityId);
                if (entityIt != entityMap.end())
                {
                    auto component = CreateComponentForType(activeComponent->typeName);
                    if (component)
                    {
                        auto *componentPtr = entityIt->second->AddComponent(component.release());
                        componentPtr->Deserialize(activeComponent->properties);
                    }
                }
                activeComponent.reset();
            }
        }

        for (const auto &pendingParent : pendingParents)
        {
            if (pendingParent.parentId == 0)
            {
                continue;
            }

            const auto entityIt = entityMap.find(pendingParent.id);
            const auto parentIt = entityMap.find(pendingParent.parentId);
            if (entityIt == entityMap.end() || parentIt == entityMap.end())
            {
                continue;
            }

            parentIt->second->AddChild(entityIt->second);
        }

        if (bakedProbeVolume.IsValid())
        {
            scene->SetBakedProbeVolume(std::move(bakedProbeVolume));
        }

        if (!environmentMapPath.empty())
        {
            auto *environmentTexture = core::Engine::GetInstance().GetTextureManager().LoadEnvironmentTextureFromFile(environmentMapPath.c_str());
            scene->SetEnvironmentMap(environmentTexture, environmentMapPath);
            scene->SetEnvironmentIntensity(environmentIntensity);
        }

        scene->MarkShadowLightsDirty();
        return scene;
    }
}