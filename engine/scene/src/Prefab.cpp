#include "PlutoGE/scene/Prefab.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/ActiveRagdollComponent.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/ClothComponent.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/IblCaptureComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/ParticleSystemComponent.h"
#include "PlutoGE/scene/components/RigidbodyComponent.h"
#include "PlutoGE/scene/components/NavAgentComponent.h"
#include "PlutoGE/scene/components/NavigationMeshComponent.h"
#include "PlutoGE/scene/components/ScriptComponent.h"
#include "PlutoGE/scene/components/SkeletonAttachmentComponent.h"
#include "PlutoGE/scene/components/SoundEmitterComponent.h"
#include "PlutoGE/scene/components/SoundListenerComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/scene/components/UIComponent.h"
#include "PlutoGE/render/Camera.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace PlutoGE::scene
{
    namespace
    {
        using ProfileClock = std::chrono::steady_clock;
        double ElapsedMs(ProfileClock::time_point start)
        {
            return std::chrono::duration<double, std::milli>(ProfileClock::now() - start).count();
        }

        struct CachedPrefab
        {
            std::filesystem::file_time_type lastWriteTime{};
            bool hasLastWriteTime = false;
            std::unique_ptr<Scene> scene;
        };

        std::unordered_map<std::string, CachedPrefab> &PrefabCache()
        {
            static std::unordered_map<std::string, CachedPrefab> cache;
            return cache;
        }

        std::mutex &PrefabStateMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        PrefabInstantiationProfile g_latestProfile;
        PrefabInstantiationProfile g_maximumProfile;
        thread_local PrefabInstantiationProfile *g_activeProfile = nullptr;

        std::string ResolvePrefabPath(std::string_view reference)
        {
            auto &assetManager = core::Engine::GetInstance().GetAssetManager();
            const std::string resolved = assetManager.ResolveAssetPath(std::string(reference));
            if (!resolved.empty())
            {
                return resolved;
            }

            return std::string(reference);
        }

        std::string ResolveComponentTypeName(const Component &component)
        {
            if (dynamic_cast<const MeshComponent *>(&component))
                return "MeshComponent";
            if (dynamic_cast<const TerrainComponent *>(&component))
                return "TerrainComponent";
            if (dynamic_cast<const ClothComponent *>(&component))
                return "ClothComponent";
            if (dynamic_cast<const FoliageComponent *>(&component))
                return "FoliageComponent";
            if (dynamic_cast<const ParticleSystemComponent *>(&component))
                return "ParticleSystemComponent";
            if (dynamic_cast<const AnimationComponent *>(&component))
                return "AnimationComponent";
            if (dynamic_cast<const ActiveRagdollComponent *>(&component))
                return "ActiveRagdollComponent";
            if (dynamic_cast<const SkeletonAttachmentComponent *>(&component))
                return "SkeletonAttachmentComponent";
            if (dynamic_cast<const CameraComponent *>(&component))
                return "CameraComponent";
            if (dynamic_cast<const LightComponent *>(&component))
                return "LightComponent";
            if (dynamic_cast<const RigidbodyComponent *>(&component))
                return "RigidbodyComponent";
            if (dynamic_cast<const NavAgentComponent *>(&component))
                return "NavAgentComponent";
            if (dynamic_cast<const NavigationMeshComponent *>(&component))
                return "NavigationMeshComponent";
            if (dynamic_cast<const ColliderComponent *>(&component))
                return "ColliderComponent";
            if (dynamic_cast<const IblCaptureComponent *>(&component))
                return "IblCaptureComponent";
            if (dynamic_cast<const ScriptComponent *>(&component))
                return "ScriptComponent";
            if (dynamic_cast<const SoundEmitterComponent *>(&component))
                return "SoundEmitterComponent";
            if (dynamic_cast<const SoundListenerComponent *>(&component))
                return "SoundListenerComponent";
            if (dynamic_cast<const CanvasComponent *>(&component))
                return "CanvasComponent";
            if (dynamic_cast<const RmlWidgetComponent *>(&component))
                return "RmlWidgetComponent";
            if (dynamic_cast<const RectTransformComponent *>(&component))
                return "RectTransformComponent";
            if (dynamic_cast<const UIImageComponent *>(&component))
                return "UIImageComponent";
            if (dynamic_cast<const UITextComponent *>(&component))
                return "UITextComponent";
            if (dynamic_cast<const UIButtonComponent *>(&component))
                return "UIButtonComponent";
            return {};
        }

        std::unique_ptr<Component> CreateComponentForType(std::string_view componentType)
        {
            if (componentType == "MeshComponent")
                return std::make_unique<MeshComponent>(MeshComponentConfig{});
            if (componentType == "TerrainComponent")
                return std::make_unique<TerrainComponent>(TerrainComponentConfig{});
            if (componentType == "ClothComponent")
                return std::make_unique<ClothComponent>();
            if (componentType == "FoliageComponent")
                return std::make_unique<FoliageComponent>();
            if (componentType == "ParticleSystemComponent")
                return std::make_unique<ParticleSystemComponent>();
            if (componentType == "AnimationComponent")
                return std::make_unique<AnimationComponent>();
            if (componentType == "ActiveRagdollComponent")
                return std::make_unique<ActiveRagdollComponent>();
            if (componentType == "SkeletonAttachmentComponent")
                return std::make_unique<SkeletonAttachmentComponent>();
            if (componentType == "CameraComponent")
                return std::make_unique<CameraComponent>(new render::Camera(render::CameraConfig{}), false);
            if (componentType == "LightComponent")
                return std::make_unique<LightComponent>();
            if (componentType == "RigidbodyComponent")
                return std::make_unique<RigidbodyComponent>();
            if (componentType == "NavAgentComponent")
                return std::make_unique<NavAgentComponent>();
            if (componentType == "NavigationMeshComponent")
                return std::make_unique<NavigationMeshComponent>();
            if (componentType == "ColliderComponent")
                return std::make_unique<ColliderComponent>();
            if (componentType == "IblCaptureComponent")
                return std::make_unique<IblCaptureComponent>();
            if (componentType == "ScriptComponent")
                return std::make_unique<ScriptComponent>(ScriptComponentConfig{});
            if (componentType == "SoundEmitterComponent")
                return std::make_unique<SoundEmitterComponent>();
            if (componentType == "SoundListenerComponent")
                return std::make_unique<SoundListenerComponent>();
            if (componentType == "CanvasComponent")
                return std::make_unique<CanvasComponent>();
            if (componentType == "RmlWidgetComponent")
                return std::make_unique<RmlWidgetComponent>();
            if (componentType == "RectTransformComponent")
                return std::make_unique<RectTransformComponent>();
            if (componentType == "UIImageComponent")
                return std::make_unique<UIImageComponent>();
            if (componentType == "UITextComponent")
                return std::make_unique<UITextComponent>();
            if (componentType == "UIButtonComponent")
                return std::make_unique<UIButtonComponent>();
            return nullptr;
        }

        void RemoveAllComponents(Entity &entity)
        {
            std::vector<Component *> components;
            for (const auto &bucket : entity.GetComponentBuckets())
            {
                components.insert(components.end(), bucket.begin(), bucket.end());
            }

            for (auto *component : components)
            {
                entity.RemoveComponent(component);
            }
        }

        void CopyComponent(Component &destination, const Component &source)
        {
            destination.SetEnabled(source.IsEnabled());
            auto properties = source.Serialize();
            destination.Deserialize(properties);
        }

        void CopyEntityFields(Entity &destination, const Entity &source)
        {
            destination.SetName(source.GetName());
            destination.SetTags(source.GetTags());
            destination.SetActive(source.IsSelfActive());
            destination.SetPosition(source.GetPosition());
            destination.SetRotation(source.GetRotation());
            destination.SetScale(source.GetScale());

            RemoveAllComponents(destination);
            for (const auto &bucket : source.GetComponentBuckets())
            {
                for (const auto *sourceComponent : bucket)
                {
                    if (!sourceComponent)
                    {
                        continue;
                    }

                    auto typeName = ResolveComponentTypeName(*sourceComponent);
                    const auto constructionStart = ProfileClock::now();
                    auto component = CreateComponentForType(typeName);
                    const double constructionMs = ElapsedMs(constructionStart);
                    if (g_activeProfile)
                        g_activeProfile->componentConstructionMs += constructionMs;
                    if (!component)
                    {
                        continue;
                    }

                    auto *destinationComponent = destination.AddComponent(component.release());
                    const auto deserializeStart = ProfileClock::now();
                    CopyComponent(*destinationComponent, *sourceComponent);
                    const double componentMs = constructionMs + ElapsedMs(deserializeStart);
                    if (g_activeProfile)
                    {
                        g_activeProfile->componentDeserializationMs += componentMs - constructionMs;
                        g_activeProfile->componentTypeTotalsMs[typeName] += componentMs;
                        if (componentMs > g_activeProfile->slowestComponentMs)
                        {
                            g_activeProfile->slowestComponentMs = componentMs;
                            g_activeProfile->slowestComponentType = typeName;
                        }
                    }
                }
            }
        }

        std::unique_ptr<Entity> CloneEntityRecursive(const Entity &source,
                                                     const std::string &prefabReference,
                                                     bool isRoot)
        {
            auto clone = std::make_unique<Entity>(EntityConfig{.name = source.GetName()});
            clone->SetActive(source.IsSelfActive());
            clone->SetPosition(source.GetPosition());
            clone->SetRotation(source.GetRotation());
            clone->SetScale(source.GetScale());
            clone->SetPrefabLink(prefabReference, source.GetID(), isRoot);

            for (const auto &bucket : source.GetComponentBuckets())
            {
                for (const auto *sourceComponent : bucket)
                {
                    if (!sourceComponent)
                    {
                        continue;
                    }

                    auto component = CreateComponentForType(ResolveComponentTypeName(*sourceComponent));
                    if (!component)
                    {
                        continue;
                    }

                    auto *cloneComponent = clone->AddComponent(component.release());
                    CopyComponent(*cloneComponent, *sourceComponent);
                }
            }

            for (auto *sourceChild : source.GetChildren())
            {
                if (!sourceChild)
                {
                    continue;
                }

                auto child = CloneEntityRecursive(*sourceChild, prefabReference, false);
                auto *childPtr = child.get();
                clone->AddChild(childPtr);
                child.release();
            }

            return clone;
        }

        Entity *CloneEntityTreeIntoScene(Scene &scene,
                                         const Entity &source,
                                         const std::string &prefabReference,
                                         Entity *parent,
                                         bool isRoot)
        {
            auto clone = std::make_unique<Entity>(EntityConfig{.name = source.GetName()});
            auto *clonePtr = clone.get();
            scene.AddEntity(std::move(clone), parent);
            CopyEntityFields(*clonePtr, source);
            clonePtr->SetPrefabLink(prefabReference, source.GetID(), isRoot);

            for (auto *sourceChild : source.GetChildren())
            {
                if (sourceChild)
                {
                    CloneEntityTreeIntoScene(scene, *sourceChild, prefabReference, clonePtr, false);
                }
            }

            return clonePtr;
        }

        Entity *CloneEntityTreeIntoScenePreservingIds(Scene &scene, const Entity &source, Entity *parent)
        {
            const EntityID serializedId = source.GetPrefabEntityID() != 0 ? source.GetPrefabEntityID() : source.GetID();
            auto clone = std::make_unique<Entity>(serializedId, EntityConfig{.name = source.GetName()});
            auto *clonePtr = clone.get();
            scene.AddEntity(std::move(clone), parent);
            CopyEntityFields(*clonePtr, source);
            clonePtr->SetPrefabLink(source.GetPrefabSource(), source.GetPrefabEntityID(), source.IsPrefabInstanceRoot());
            for (const auto &overridePath : source.GetPrefabOverrides())
            {
                clonePtr->AddPrefabOverride(overridePath);
            }

            for (auto *sourceChild : source.GetChildren())
            {
                if (sourceChild)
                {
                    CloneEntityTreeIntoScenePreservingIds(scene, *sourceChild, clonePtr);
                }
            }

            return clonePtr;
        }

        Entity *DuplicateEntityTreeIntoScene(Scene &scene, const Entity &source, Entity *parent, bool preservePrefabLink)
        {
            auto clone = std::make_unique<Entity>(EntityConfig{.name = source.GetName()});
            auto *clonePtr = clone.get();
            scene.AddEntity(std::move(clone), parent);
            CopyEntityFields(*clonePtr, source);
            if (preservePrefabLink && !source.GetPrefabSource().empty())
            {
                clonePtr->SetPrefabLink(source.GetPrefabSource(), source.GetPrefabEntityID(), source.IsPrefabInstanceRoot());
                for (const auto &overridePath : source.GetPrefabOverrides())
                {
                    clonePtr->AddPrefabOverride(overridePath);
                }
            }

            for (auto *sourceChild : source.GetChildren())
            {
                if (sourceChild)
                {
                    DuplicateEntityTreeIntoScene(scene, *sourceChild, clonePtr, preservePrefabLink);
                }
            }

            return clonePtr;
        }

        void BuildEntityIdRemap(const Entity &source,
                                const Entity &clone,
                                std::unordered_map<EntityID, EntityID> &entityIdRemap)
        {
            entityIdRemap[source.GetID()] = clone.GetID();

            const auto &sourceChildren = source.GetChildren();
            const auto &cloneChildren = clone.GetChildren();
            const std::size_t childCount = std::min(sourceChildren.size(), cloneChildren.size());
            for (std::size_t childIndex = 0; childIndex < childCount; ++childIndex)
            {
                if (sourceChildren[childIndex] && cloneChildren[childIndex])
                {
                    BuildEntityIdRemap(*sourceChildren[childIndex], *cloneChildren[childIndex], entityIdRemap);
                }
            }
        }

        void RemapScriptEntityReferences(Entity &entity,
                                         const std::unordered_map<EntityID, EntityID> &entityIdRemap)
        {
            for (const auto &bucket : entity.GetComponentBuckets())
            {
                for (auto *component : bucket)
                {
                    auto *navAgent = dynamic_cast<NavAgentComponent *>(component);
                    if (!navAgent)
                    {
                        continue;
                    }

                    auto properties = navAgent->Serialize();
                    for (auto &property : properties)
                    {
                        if (property.name != "Target Entity")
                        {
                            continue;
                        }

                        try
                        {
                            const auto sourceId = static_cast<EntityID>(std::stoul(property.value));
                            if (const auto remapped = entityIdRemap.find(sourceId);
                                remapped != entityIdRemap.end())
                            {
                                property.value = std::to_string(remapped->second);
                            }
                        }
                        catch (...)
                        {
                        }
                    }
                    navAgent->Deserialize(properties);
                }
            }

            for (auto *scriptComponent : entity.GetComponents<ScriptComponent>())
            {
                if (scriptComponent)
                {
                    scriptComponent->RemapEntityReferences(entityIdRemap);
                }
            }

            for (auto *child : entity.GetChildren())
            {
                if (child)
                {
                    RemapScriptEntityReferences(*child, entityIdRemap);
                }
            }
        }

        void RemapClonedScriptEntityReferences(const Entity &source, Entity &clone)
        {
            std::unordered_map<EntityID, EntityID> entityIdRemap;
            BuildEntityIdRemap(source, clone, entityIdRemap);
            RemapScriptEntityReferences(clone, entityIdRemap);
        }

        std::unique_ptr<Scene> LoadPrefabScene(std::string_view prefabReference, std::string *errorMessage)
        {
            return SceneSerializer::Load(ResolvePrefabPath(prefabReference), errorMessage);
        }

        Scene *LoadCachedPrefabScene(std::string_view prefabReference, std::string *errorMessage,
                                     bool *cacheHit = nullptr, double *resolutionMs = nullptr,
                                     double *parsingMs = nullptr, std::string *resolvedPathOut = nullptr)
        {
            const auto resolutionStart = ProfileClock::now();
            const std::string resolvedPath = ResolvePrefabPath(prefabReference);
            if (resolutionMs) *resolutionMs = ElapsedMs(resolutionStart);
            if (resolvedPathOut) *resolvedPathOut = resolvedPath;
            std::error_code timestampError;
            const auto lastWriteTime = std::filesystem::last_write_time(resolvedPath, timestampError);
            const bool hasLastWriteTime = !timestampError;

            std::scoped_lock lock(PrefabStateMutex());
            auto &cache = PrefabCache();
            auto cached = cache.find(resolvedPath);
            if (cached != cache.end() &&
                cached->second.hasLastWriteTime == hasLastWriteTime &&
                (!hasLastWriteTime || cached->second.lastWriteTime == lastWriteTime))
            {
                if (cacheHit) *cacheHit = true;
                if (parsingMs) *parsingMs = 0.0;
                return cached->second.scene.get();
            }

            if (cacheHit) *cacheHit = false;
            const auto parsingStart = ProfileClock::now();
            auto loadedScene = SceneSerializer::Load(resolvedPath, errorMessage);
            if (parsingMs) *parsingMs = ElapsedMs(parsingStart);
            if (!loadedScene)
            {
                return nullptr;
            }

            CachedPrefab entry{
                .lastWriteTime = lastWriteTime,
                .hasLastWriteTime = hasLastWriteTime,
                .scene = std::move(loadedScene),
            };
            auto [iterator, inserted] = cache.insert_or_assign(resolvedPath, std::move(entry));
            (void)inserted;
            return iterator->second.scene.get();
        }

        Entity *FindPrefabEntity(Scene &prefabScene, EntityID prefabEntityId)
        {
            if (prefabEntityId != 0)
            {
                if (auto *entity = prefabScene.FindEntityByID(prefabEntityId))
                {
                    return entity;
                }
            }

            const auto roots = prefabScene.GetRootEntities();
            return roots.empty() ? nullptr : roots.front();
        }

        Component *FindFirstComponentByType(Entity &entity, std::string_view componentType)
        {
            for (const auto &bucket : entity.GetComponentBuckets())
            {
                for (auto *component : bucket)
                {
                    if (component && ResolveComponentTypeName(*component) == componentType)
                    {
                        return component;
                    }
                }
            }

            return nullptr;
        }

        const Component *FindFirstComponentByType(const Entity &entity, std::string_view componentType)
        {
            for (const auto &bucket : entity.GetComponentBuckets())
            {
                for (auto *component : bucket)
                {
                    if (component && ResolveComponentTypeName(*component) == componentType)
                    {
                        return component;
                    }
                }
            }

            return nullptr;
        }

        std::optional<std::string> CaptureOverrideValue(const Entity &entity, std::string_view path)
        {
            if (path == "Name")
                return entity.GetName();
            if (path == "Tags")
            {
                std::string tags;
                for (const auto &tag : entity.GetTags())
                {
                    if (!tags.empty())
                    {
                        tags += "\n";
                    }
                    tags += tag;
                }
                return tags;
            }
            if (path == "Active")
                return entity.IsSelfActive() ? "1" : "0";
            if (path == "Transform.Position")
            {
                const auto value = entity.GetPosition();
                return std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z);
            }
            if (path == "Transform.Rotation")
            {
                const auto value = entity.GetRotation();
                return std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z);
            }
            if (path == "Transform.Scale")
            {
                const auto value = entity.GetScale();
                return std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z);
            }

            constexpr std::string_view prefix = "Component:";
            if (path.rfind(prefix, 0) != 0)
            {
                return std::nullopt;
            }

            const auto remainder = path.substr(prefix.size());
            const auto separator = remainder.find(':');
            if (separator == std::string_view::npos)
            {
                return std::nullopt;
            }

            const auto componentType = remainder.substr(0, separator);
            const auto propertyName = remainder.substr(separator + 1);
            const auto *component = FindFirstComponentByType(entity, componentType);
            if (!component)
            {
                return std::nullopt;
            }

            if (propertyName == "Enabled")
            {
                return component->IsEnabled() ? "1" : "0";
            }

            for (const auto &property : component->Serialize())
            {
                if (property.name == propertyName)
                {
                    return property.value;
                }
            }

            return std::nullopt;
        }

        void ApplyOverrideValue(Entity &entity, std::string_view path, std::string_view value)
        {
            if (path == "Name")
            {
                entity.SetName(std::string(value));
                return;
            }
            if (path == "Tags")
            {
                std::vector<std::string> tags;
                std::string text(value);
                std::size_t start = 0;
                while (start <= text.size())
                {
                    const auto end = text.find('\n', start);
                    auto tag = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
                    if (!tag.empty() && std::find(tags.begin(), tags.end(), tag) == tags.end())
                    {
                        tags.push_back(std::move(tag));
                    }
                    if (end == std::string::npos)
                    {
                        break;
                    }
                    start = end + 1;
                }
                entity.SetTags(std::move(tags));
                return;
            }
            if (path == "Active")
            {
                entity.SetActive(value == "1" || value == "true" || value == "True");
                return;
            }
            if (path == "Transform.Position" || path == "Transform.Rotation" || path == "Transform.Scale")
            {
                glm::vec3 parsed{0.0f};
                std::sscanf(std::string(value).c_str(), "%f,%f,%f", &parsed.x, &parsed.y, &parsed.z);
                if (path == "Transform.Position")
                    entity.SetPosition(parsed);
                else if (path == "Transform.Rotation")
                    entity.SetRotation(parsed);
                else
                    entity.SetScale(parsed);
                return;
            }

            constexpr std::string_view prefix = "Component:";
            if (path.rfind(prefix, 0) != 0)
            {
                return;
            }

            const auto remainder = path.substr(prefix.size());
            const auto separator = remainder.find(':');
            if (separator == std::string_view::npos)
            {
                return;
            }

            const auto componentType = remainder.substr(0, separator);
            const auto propertyName = remainder.substr(separator + 1);
            auto *component = FindFirstComponentByType(entity, componentType);
            if (!component)
            {
                return;
            }

            if (propertyName == "Enabled")
            {
                component->SetEnabled(value == "1" || value == "true" || value == "True");
                return;
            }

            auto properties = component->Serialize();
            for (auto &property : properties)
            {
                if (property.name == propertyName)
                {
                    property.value = std::string(value);
                    component->Deserialize(properties);
                    return;
                }
            }
        }

        void CollectPrefabRoots(Entity *entity, std::vector<Entity *> &roots, std::string_view prefabReference)
        {
            if (!entity)
            {
                return;
            }

            if (entity->IsPrefabInstanceRoot() &&
                (prefabReference.empty() || entity->GetPrefabSource() == prefabReference))
            {
                roots.push_back(entity);
                return;
            }

            for (auto *child : entity->GetChildren())
            {
                CollectPrefabRoots(child, roots, prefabReference);
            }
        }

        void CollectEntitiesRecursive(Entity *entity, std::vector<Entity *> &entities)
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

        Entity *FindEntityByPrefabId(Entity *entity, EntityID prefabEntityId)
        {
            if (!entity)
            {
                return nullptr;
            }

            if (entity->GetPrefabEntityID() == prefabEntityId)
            {
                return entity;
            }

            for (auto *child : entity->GetChildren())
            {
                if (auto *match = FindEntityByPrefabId(child, prefabEntityId))
                {
                    return match;
                }
            }

            return nullptr;
        }

        bool SaveSingleEntityScene(const Entity &entity,
                                   const std::filesystem::path &filePath,
                                   bool clearPrefabLink,
                                   std::string *errorMessage)
        {
            std::error_code errorCode;
            std::filesystem::create_directories(filePath.parent_path(), errorCode);
            if (errorCode)
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to create prefab directory: " + filePath.parent_path().string();
                }
                return false;
            }

            Scene prefabScene;
            auto *clonedRoot = CloneEntityTreeIntoScenePreservingIds(prefabScene, entity, nullptr);
            if (clonedRoot)
            {
                RemapClonedScriptEntityReferences(entity, *clonedRoot);
            }
            if (auto roots = prefabScene.GetRootEntities(); !roots.empty() && clearPrefabLink)
            {
                roots.front()->ClearPrefabLinkRecursive();
            }

            return SceneSerializer::Save(prefabScene, filePath.string(), errorMessage);
        }
    }

    PrefabPreloadResult Prefab::Preload(std::string_view prefabReference)
    {
        PrefabPreloadResult result;
        const auto start = ProfileClock::now();
        result.ready = LoadCachedPrefabScene(prefabReference, &result.error, &result.cacheHit,
                                             nullptr, nullptr, &result.resolvedPath) != nullptr;
        result.durationMs = ElapsedMs(start);
        return result;
    }

    bool Prefab::IsReady(std::string_view prefabReference)
    {
        const std::string resolvedPath = ResolvePrefabPath(prefabReference);
        std::error_code timestampError;
        const auto lastWriteTime = std::filesystem::last_write_time(resolvedPath, timestampError);
        const bool hasLastWriteTime = !timestampError;
        std::scoped_lock lock(PrefabStateMutex());
        const auto cached = PrefabCache().find(resolvedPath);
        return cached != PrefabCache().end() && cached->second.scene &&
               cached->second.hasLastWriteTime == hasLastWriteTime &&
               (!hasLastWriteTime || cached->second.lastWriteTime == lastWriteTime);
    }

    PrefabInstantiationProfile Prefab::GetLatestInstantiationProfile()
    {
        std::scoped_lock lock(PrefabStateMutex());
        return g_latestProfile;
    }

    PrefabInstantiationProfile Prefab::GetMaximumInstantiationProfile()
    {
        std::scoped_lock lock(PrefabStateMutex());
        return g_maximumProfile;
    }

    void Prefab::ResetInstantiationProfiles()
    {
        std::scoped_lock lock(PrefabStateMutex());
        g_latestProfile = {};
        g_maximumProfile = {};
    }

    bool Prefab::SaveFromEntity(const Entity &entity,
                                const std::filesystem::path &filePath,
                                std::string *errorMessage)
    {
        return SaveSingleEntityScene(entity, filePath, true, errorMessage);
    }

    Entity *Prefab::Instantiate(Scene &scene,
                                std::string prefabReference,
                                Entity *parent,
                                std::string *errorMessage)
    {
        PrefabInstantiationProfile profile;
        const auto totalStart = ProfileClock::now();
        bool cacheHit = false;
        auto *prefabScene = LoadCachedPrefabScene(prefabReference, errorMessage, &cacheHit,
                                                  &profile.fileResolutionMs, &profile.parsingMs,
                                                  &profile.prefabPath);
        profile.parsedPrefabCacheMiss = !cacheHit;
        profile.synchronousLoadCount = cacheHit ? 0u : 1u;
        if (!prefabScene)
        {
            return nullptr;
        }

        const auto roots = prefabScene->GetRootEntities();
        if (roots.empty())
        {
            if (errorMessage)
            {
                *errorMessage = "Prefab has no root entity.";
            }
            return nullptr;
        }

        g_activeProfile = &profile;
        const auto hierarchyStart = ProfileClock::now();
        auto *instanceRoot = CloneEntityTreeIntoScene(scene, *roots.front(), prefabReference, parent, true);
        profile.hierarchyAllocationMs = std::max(0.0, ElapsedMs(hierarchyStart) -
                                                       profile.componentConstructionMs -
                                                       profile.componentDeserializationMs);
        if (instanceRoot)
        {
            const auto remapStart = ProfileClock::now();
            RemapClonedScriptEntityReferences(*roots.front(), *instanceRoot);
            profile.referenceRemappingMs = ElapsedMs(remapStart);
            profile.rootEntityName = instanceRoot->GetName();
            profile.rootEntityId = instanceRoot->GetID();
        }
        g_activeProfile = nullptr;
        profile.totalMs = ElapsedMs(totalStart);
        {
            std::scoped_lock lock(PrefabStateMutex());
            g_latestProfile = profile;
            if (profile.totalMs >= g_maximumProfile.totalMs)
                g_maximumProfile = profile;
        }
        return instanceRoot;
    }

    Entity *Prefab::DuplicateEntity(Scene &scene,
                                    const Entity &source,
                                    Entity *parent,
                                    bool preservePrefabLink)
    {
        auto *duplicateRoot = DuplicateEntityTreeIntoScene(scene, source, parent, preservePrefabLink);
        if (duplicateRoot)
        {
            RemapClonedScriptEntityReferences(source, *duplicateRoot);
        }
        return duplicateRoot;
    }

    bool Prefab::UpdateInstance(Entity &instanceRoot, std::string *errorMessage)
    {
        if (!instanceRoot.IsPrefabInstanceRoot() || instanceRoot.GetPrefabSource().empty())
        {
            if (errorMessage)
            {
                *errorMessage = "Selected entity is not a prefab instance root.";
            }
            return false;
        }

        auto *scene = instanceRoot.GetScene();
        if (!scene)
        {
            if (errorMessage)
            {
                *errorMessage = "Prefab instance is not in a scene.";
            }
            return false;
        }

        const std::string prefabReference = instanceRoot.GetPrefabSource();
        auto prefabScene = LoadPrefabScene(prefabReference, errorMessage);
        if (!prefabScene)
        {
            return false;
        }

        auto *prefabRoot = FindPrefabEntity(*prefabScene, instanceRoot.GetPrefabEntityID());
        if (!prefabRoot)
        {
            if (errorMessage)
            {
                *errorMessage = "Prefab root entity was not found.";
            }
            return false;
        }

        std::unordered_map<EntityID, std::vector<std::pair<std::string, std::string>>> overrideValues;
        std::unordered_map<EntityID, EntityID> previousInstanceToPrefabId;
        std::vector<Entity *> existingEntities;
        CollectEntitiesRecursive(&instanceRoot, existingEntities);
        for (auto *entity : existingEntities)
        {
            if (!entity || entity->GetPrefabEntityID() == 0)
            {
                continue;
            }

            previousInstanceToPrefabId[entity->GetID()] = entity->GetPrefabEntityID();

            for (const auto &path : entity->GetPrefabOverrides())
            {
                if (auto value = CaptureOverrideValue(*entity, path))
                {
                    overrideValues[entity->GetPrefabEntityID()].push_back({path, *value});
                }
            }
        }

        const auto previousChildren = instanceRoot.GetChildren();
        for (auto *child : previousChildren)
        {
            scene->RemoveEntity(child);
        }

        CopyEntityFields(instanceRoot, *prefabRoot);
        instanceRoot.SetPrefabLink(prefabReference, prefabRoot->GetID(), true);
        instanceRoot.ClearPrefabOverrides();

        for (auto *sourceChild : prefabRoot->GetChildren())
        {
            if (sourceChild)
            {
                CloneEntityTreeIntoScene(*scene, *sourceChild, prefabReference, &instanceRoot, false);
            }
        }

        RemapClonedScriptEntityReferences(*prefabRoot, instanceRoot);

        for (const auto &[prefabEntityId, values] : overrideValues)
        {
            auto *target = FindEntityByPrefabId(&instanceRoot, prefabEntityId);
            if (!target)
            {
                continue;
            }

            for (const auto &[path, value] : values)
            {
                ApplyOverrideValue(*target, path, value);
                target->AddPrefabOverride(path);
            }
        }

        // Overrides are captured before the old children are replaced, so their
        // entity-reference values still contain the previous instance IDs.
        std::unordered_map<EntityID, EntityID> previousToUpdatedEntityId;
        for (const auto &[previousEntityId, prefabEntityId] : previousInstanceToPrefabId)
        {
            if (auto *updatedEntity = FindEntityByPrefabId(&instanceRoot, prefabEntityId))
            {
                previousToUpdatedEntityId[previousEntityId] = updatedEntity->GetID();
            }
        }
        RemapScriptEntityReferences(instanceRoot, previousToUpdatedEntityId);

        return true;
    }

    int Prefab::UpdateInstances(Scene &scene,
                                std::string_view prefabReference,
                                std::string *errorMessage)
    {
        std::vector<Entity *> roots;
        for (auto *rootEntity : scene.GetRootEntities())
        {
            CollectPrefabRoots(rootEntity, roots, prefabReference);
        }

        int updatedCount = 0;
        for (auto *root : roots)
        {
            if (root && UpdateInstance(*root, errorMessage))
            {
                ++updatedCount;
            }
        }

        return updatedCount;
    }

    bool Prefab::ApplyInstanceToPrefab(const Entity &instanceRoot, std::string *errorMessage)
    {
        if (!instanceRoot.IsPrefabInstanceRoot() || instanceRoot.GetPrefabSource().empty())
        {
            if (errorMessage)
            {
                *errorMessage = "Selected entity is not a prefab instance root.";
            }
            return false;
        }

        return SaveSingleEntityScene(instanceRoot, ResolvePrefabPath(instanceRoot.GetPrefabSource()), true, errorMessage);
    }
}
