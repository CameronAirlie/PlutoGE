#include "PlutoGE/scene/SceneSerializer.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/ClothComponent.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/IblCaptureComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/OceanComponent.h"
#include "PlutoGE/scene/components/ParticleSystemComponent.h"
#include "PlutoGE/scene/components/PhysicalSkyComponent.h"
#include "PlutoGE/scene/components/RigidbodyComponent.h"
#include "PlutoGE/scene/components/NavAgentComponent.h"
#include "PlutoGE/scene/components/NavigationMeshComponent.h"
#include "PlutoGE/scene/components/ScriptComponent.h"
#include "PlutoGE/scene/components/SkeletonAttachmentComponent.h"
#include "PlutoGE/scene/components/SoundEmitterComponent.h"
#include "PlutoGE/scene/components/SoundListenerComponent.h"
#include "PlutoGE/scene/components/SplineComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/scene/components/UIComponent.h"
#include "PlutoGE/scene/components/VolumetricCloudComponent.h"
#include "PlutoGE/render/Camera.h"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <system_error>
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
            parts.reserve(8);
            current.reserve(text.size());
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

        std::string_view NextCsvToken(std::string_view &value)
        {
            const auto delimiter = value.find(',');
            if (delimiter == std::string_view::npos)
            {
                const std::string_view token = value;
                value = {};
                return token;
            }

            const std::string_view token = value.substr(0, delimiter);
            value.remove_prefix(delimiter + 1);
            return token;
        }

        template <typename T>
        bool ParseNumber(std::string_view token, T &output)
        {
            const char *begin = token.data();
            const char *end = begin + token.size();
            const auto result = std::from_chars(begin, end, output);
            return result.ec == std::errc{} && result.ptr == end;
        }

        std::string SerializeVec3(const glm::vec3 &value)
        {
            return std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z);
        }

        glm::vec3 ParseVec3(std::string_view value)
        {
            glm::vec3 parsedValue{0.0f};
            ParseNumber(NextCsvToken(value), parsedValue.x);
            ParseNumber(NextCsvToken(value), parsedValue.y);
            ParseNumber(NextCsvToken(value), parsedValue.z);
            return parsedValue;
        }

        glm::ivec3 ParseIVec3(std::string_view value)
        {
            glm::ivec3 parsedValue{0};
            ParseNumber(NextCsvToken(value), parsedValue.x);
            ParseNumber(NextCsvToken(value), parsedValue.y);
            ParseNumber(NextCsvToken(value), parsedValue.z);
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
            if (dynamic_cast<const TerrainComponent *>(&component))
            {
                return "TerrainComponent";
            }
            if (dynamic_cast<const ClothComponent *>(&component))
            {
                return "ClothComponent";
            }
            if (dynamic_cast<const OceanComponent *>(&component))
            {
                return "OceanComponent";
            }
            if (dynamic_cast<const SplineComponent *>(&component))
            {
                return "SplineComponent";
            }
            if (dynamic_cast<const FoliageComponent *>(&component))
            {
                return "FoliageComponent";
            }
            if (dynamic_cast<const ParticleSystemComponent *>(&component))
            {
                return "ParticleSystemComponent";
            }
            if (dynamic_cast<const AnimationComponent *>(&component))
            {
                return "AnimationComponent";
            }
            if (dynamic_cast<const SkeletonAttachmentComponent *>(&component))
            {
                return "SkeletonAttachmentComponent";
            }
            if (dynamic_cast<const CameraComponent *>(&component))
            {
                return "CameraComponent";
            }
            if (dynamic_cast<const LightComponent *>(&component))
            {
                return "LightComponent";
            }
            if (dynamic_cast<const RigidbodyComponent *>(&component))
            {
                return "RigidbodyComponent";
            }
            if (dynamic_cast<const NavAgentComponent *>(&component))
                return "NavAgentComponent";
            if (dynamic_cast<const NavigationMeshComponent *>(&component))
                return "NavigationMeshComponent";
            if (dynamic_cast<const ColliderComponent *>(&component))
            {
                return "ColliderComponent";
            }
            if (dynamic_cast<const IblCaptureComponent *>(&component))
            {
                return "IblCaptureComponent";
            }
            if (dynamic_cast<const ScriptComponent *>(&component))
            {
                return "ScriptComponent";
            }
            if (dynamic_cast<const SoundEmitterComponent *>(&component))
            {
                return "SoundEmitterComponent";
            }
            if (dynamic_cast<const SoundListenerComponent *>(&component))
            {
                return "SoundListenerComponent";
            }
            if (dynamic_cast<const CanvasComponent *>(&component))
            {
                return "CanvasComponent";
            }
            if (dynamic_cast<const RectTransformComponent *>(&component))
            {
                return "RectTransformComponent";
            }
            if (dynamic_cast<const UIImageComponent *>(&component))
            {
                return "UIImageComponent";
            }
            if (dynamic_cast<const UITextComponent *>(&component))
            {
                return "UITextComponent";
            }
            if (dynamic_cast<const UIButtonComponent *>(&component))
            {
                return "UIButtonComponent";
            }
            if (dynamic_cast<const VolumetricCloudComponent *>(&component))
            {
                return "VolumetricCloudComponent";
            }
            if (dynamic_cast<const PhysicalSkyComponent *>(&component))
            {
                return "PhysicalSkyComponent";
            }

            return {};
        }

        std::unique_ptr<Component> CreateComponentForType(std::string_view componentType)
        {
            if (componentType == "MeshComponent")
            {
                return std::make_unique<MeshComponent>(MeshComponentConfig{});
            }
            if (componentType == "TerrainComponent")
            {
                return std::make_unique<TerrainComponent>(TerrainComponentConfig{});
            }
            if (componentType == "ClothComponent")
            {
                return std::make_unique<ClothComponent>();
            }
            if (componentType == "OceanComponent")
            {
                return std::make_unique<OceanComponent>();
            }
            if (componentType == "SplineComponent")
            {
                return std::make_unique<SplineComponent>(SplineComponentConfig{});
            }
            if (componentType == "FoliageComponent")
            {
                return std::make_unique<FoliageComponent>();
            }
            if (componentType == "ParticleSystemComponent")
            {
                return std::make_unique<ParticleSystemComponent>();
            }
            if (componentType == "AnimationComponent")
            {
                return std::make_unique<AnimationComponent>();
            }
            if (componentType == "SkeletonAttachmentComponent")
            {
                return std::make_unique<SkeletonAttachmentComponent>();
            }
            if (componentType == "CameraComponent")
            {
                return std::make_unique<CameraComponent>(new render::Camera(render::CameraConfig{}));
            }
            if (componentType == "LightComponent")
            {
                return std::make_unique<LightComponent>();
            }
            if (componentType == "RigidbodyComponent")
            {
                return std::make_unique<RigidbodyComponent>();
            }
            if (componentType == "NavAgentComponent")
                return std::make_unique<NavAgentComponent>();
            if (componentType == "NavigationMeshComponent")
                return std::make_unique<NavigationMeshComponent>();
            if (componentType == "ColliderComponent")
            {
                return std::make_unique<ColliderComponent>();
            }
            if (componentType == "IblCaptureComponent")
            {
                return std::make_unique<IblCaptureComponent>();
            }
            if (componentType == "ScriptComponent")
            {
                return std::make_unique<ScriptComponent>(ScriptComponentConfig{});
            }
            if (componentType == "SoundEmitterComponent")
            {
                return std::make_unique<SoundEmitterComponent>();
            }
            if (componentType == "SoundListenerComponent")
            {
                return std::make_unique<SoundListenerComponent>();
            }
            if (componentType == "CanvasComponent")
            {
                return std::make_unique<CanvasComponent>();
            }
            if (componentType == "RectTransformComponent")
            {
                return std::make_unique<RectTransformComponent>();
            }
            if (componentType == "UIImageComponent")
            {
                return std::make_unique<UIImageComponent>();
            }
            if (componentType == "UITextComponent")
            {
                return std::make_unique<UITextComponent>();
            }
            if (componentType == "UIButtonComponent")
            {
                return std::make_unique<UIButtonComponent>();
            }
            if (componentType == "VolumetricCloudComponent")
            {
                return std::make_unique<VolumetricCloudComponent>();
            }
            if (componentType == "PhysicalSkyComponent")
            {
                return std::make_unique<PhysicalSkyComponent>();
            }

            return nullptr;
        }

        bool IsAssetPathProperty(std::string_view componentType, std::string_view propertyName)
        {
            if (componentType == "MeshComponent")
            {
                return propertyName == "SourceMesh" || propertyName.ends_with("LightmapPath") || propertyName.ends_with("MaterialAsset");
            }
            if (componentType == "TerrainComponent")
            {
                return propertyName == "HeightMap" || propertyName == "MaterialAsset";
            }
            if (componentType == "ClothComponent")
            {
                return false;
            }
            if (componentType == "OceanComponent")
            {
                return false;
            }
            if (componentType == "SplineComponent")
            {
                return propertyName == "MaterialAsset";
            }
            if (componentType == "FoliageComponent")
            {
                return propertyName == "SourceMesh" ||
                       propertyName == "MaterialAsset" ||
                       propertyName.ends_with(".SourceMesh") ||
                       propertyName.ends_with(".MaterialAsset");
            }
            if (componentType == "ParticleSystemComponent")
            {
                return propertyName == "MaterialAsset";
            }
            if (componentType == "AnimationComponent")
            {
                return propertyName == "SourceAnimation" || propertyName == "AnimationGraph";
            }
            if (componentType == "UIImageComponent")
            {
                return propertyName == "TexturePath";
            }
            if (componentType == "UITextComponent")
            {
                return propertyName == "FontPath";
            }

            return false;
        }
    }

    namespace
    {
        bool SaveSceneToStream(const Scene &scene, std::ostream &output, std::string *errorMessage)
        {
            if (!output.good())
            {
                if (errorMessage)
                {
                    *errorMessage = "Scene output stream is not writable.";
                }
                return false;
            }

            output << "SCENE\t1\n";

            auto &assetManager = core::Engine::GetInstance().GetAssetManager();

            if (!scene.GetEnvironmentMapPath().empty())
            {
                output << "ENVIRONMENT\t"
                       << EscapeText(assetManager.PersistAssetPath(scene.GetEnvironmentMapPath())) << '\t'
                       << scene.GetEnvironmentIntensity() << '\n';
            }

            for (const auto &captureVolume : scene.GetIblCaptureVolumes())
            {
                if (captureVolume.environmentMapPath.empty())
                {
                    continue;
                }

                output << "IBL_CAPTURE\t"
                       << SerializeVec3(captureVolume.origin) << '\t'
                       << SerializeVec3(captureVolume.size) << '\t'
                       << EscapeText(assetManager.PersistAssetPath(captureVolume.environmentMapPath)) << '\t'
                       << captureVolume.intensity << '\t'
                       << captureVolume.blendDistance << '\n';
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
                       << (entity->IsSelfActive() ? 1 : 0) << '\t'
                       << EscapeText(entity->GetName()) << '\t'
                       << SerializeVec3(entity->GetPosition()) << '\t'
                       << SerializeVec3(entity->GetRotation()) << '\t'
                       << SerializeVec3(entity->GetScale()) << '\n';

                if (!entity->GetPrefabSource().empty())
                {
                    output << "PREFAB\t"
                           << entity->GetID() << '\t'
                           << EscapeText(entity->GetPrefabSource()) << '\t'
                           << entity->GetPrefabEntityID() << '\t'
                           << (entity->IsPrefabInstanceRoot() ? 1 : 0) << '\t'
                           << entity->GetPrefabOverrides().size();
                    for (const auto &overridePath : entity->GetPrefabOverrides())
                    {
                        output << '\t' << EscapeText(overridePath);
                    }
                    output << '\n';
                }

                if (!entity->GetTags().empty())
                {
                    output << "TAGS\t" << entity->GetID() << '\t' << entity->GetTags().size();
                    for (const auto &tag : entity->GetTags())
                    {
                        output << '\t' << EscapeText(tag);
                    }
                    output << '\n';
                }

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
                            const std::string serializedValue = IsAssetPathProperty(componentType, property.name)
                                                                    ? assetManager.PersistAssetPath(property.value)
                                                                    : property.value;
                            output << "PROPERTY\t"
                                   << EscapeText(property.name) << '\t'
                                   << static_cast<int>(property.type) << '\t'
                                   << EscapeText(serializedValue) << '\t'
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

        return SaveSceneToStream(scene, output, errorMessage);
    }

    bool SceneSerializer::SaveToString(const Scene &scene, std::string &outputText, std::string *errorMessage)
    {
        std::ostringstream output;
        if (!SaveSceneToStream(scene, output, errorMessage))
        {
            return false;
        }

        outputText = output.str();
        return true;
    }

    namespace
    {
        std::unique_ptr<Scene> LoadSceneFromStream(std::istream &input, const std::string &filePath, std::string *errorMessage)
        {
            if (!input.good())
            {
                if (errorMessage)
                {
                    *errorMessage = "Scene input stream is not readable.";
                }
                return nullptr;
            }

            auto scene = std::make_unique<Scene>();
            if (!filePath.empty())
            {
                scene->SetFilePath(std::filesystem::absolute(std::filesystem::path(filePath)).lexically_normal().string());
            }

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
            auto &assetManager = core::Engine::GetInstance().GetAssetManager();
            BakedProbeVolume bakedProbeVolume;
            std::string environmentMapPath;
            float environmentIntensity = 1.0f;
            std::vector<IblCaptureVolume> iblCaptureVolumes;

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
                    environmentMapPath = assetManager.ResolveAssetPath(tokens[1]);
                    environmentIntensity = std::stof(tokens[2]);
                    continue;
                }

                if (tokens[0] == "IBL_CAPTURE" && tokens.size() >= 6)
                {
                    IblCaptureVolume captureVolume;
                    captureVolume.origin = ParseVec3(tokens[1]);
                    captureVolume.size = ParseVec3(tokens[2]);
                    captureVolume.environmentMapPath = assetManager.ResolveAssetPath(tokens[3]);
                    captureVolume.intensity = std::stof(tokens[4]);
                    captureVolume.blendDistance = std::stof(tokens[5]);
                    iblCaptureVolumes.push_back(std::move(captureVolume));
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

                    auto entity = std::make_unique<Entity>(serializedId, EntityConfig{.name = tokens[4]});
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

                if (tokens[0] == "PREFAB" && tokens.size() >= 5)
                {
                    const EntityID entityId = static_cast<EntityID>(std::stoul(tokens[1]));
                    const auto entityIt = entityMap.find(entityId);
                    if (entityIt != entityMap.end())
                    {
                        entityIt->second->SetPrefabLink(tokens[2],
                                                        static_cast<EntityID>(std::stoul(tokens[3])),
                                                        tokens[4] == "1");
                        if (tokens.size() >= 6)
                        {
                            const int overrideCount = std::stoi(tokens[5]);
                            for (int overrideIndex = 0; overrideIndex < overrideCount && 6 + overrideIndex < static_cast<int>(tokens.size()); ++overrideIndex)
                            {
                                entityIt->second->AddPrefabOverride(tokens[6 + overrideIndex]);
                            }
                        }
                    }
                    continue;
                }

                if (tokens[0] == "TAGS" && tokens.size() >= 3)
                {
                    const EntityID entityId = static_cast<EntityID>(std::stoul(tokens[1]));
                    const auto entityIt = entityMap.find(entityId);
                    if (entityIt != entityMap.end())
                    {
                        std::vector<std::string> tags;
                        const int tagCount = std::stoi(tokens[2]);
                        tags.reserve(static_cast<std::size_t>(std::max(tagCount, 0)));
                        for (int tagIndex = 0; tagIndex < tagCount && 3 + tagIndex < static_cast<int>(tokens.size()); ++tagIndex)
                        {
                            if (!tokens[3 + tagIndex].empty())
                            {
                                tags.push_back(tokens[3 + tagIndex]);
                            }
                        }
                        entityIt->second->SetTags(std::move(tags));
                    }
                    continue;
                }

                if (tokens[0] == "PROPERTY" && tokens.size() >= 5 && activeComponent.has_value())
                {
                    Property property;
                    property.name = tokens[1];
                    property.type = static_cast<PropertyType>(std::stoi(tokens[2]));
                    property.value = IsAssetPathProperty(activeComponent->typeName, property.name)
                                         ? assetManager.ResolveAssetPath(tokens[3])
                                         : tokens[3];
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

            for (auto &captureVolume : iblCaptureVolumes)
            {
                if (scene->GetIblCaptureVolumes().size() >= static_cast<std::size_t>(kMaxIblCaptureVolumes))
                {
                    break;
                }

                captureVolume.environmentMapTexture = core::Engine::GetInstance().GetTextureManager().LoadEnvironmentTextureFromFile(captureVolume.environmentMapPath.c_str());
                scene->AddIblCaptureVolume(std::move(captureVolume));
            }

            scene->MarkShadowLightsDirty();
            return scene;
        }
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

        return LoadSceneFromStream(input, filePath, errorMessage);
    }

    std::unique_ptr<Scene> SceneSerializer::LoadFromString(const std::string &text, std::string *errorMessage)
    {
        std::istringstream input(text);
        return LoadSceneFromStream(input, {}, errorMessage);
    }
}
