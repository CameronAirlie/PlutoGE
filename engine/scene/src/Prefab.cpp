#include "PlutoGE/scene/Prefab.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
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
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/scene/components/UIComponent.h"
#include "PlutoGE/render/Camera.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace PlutoGE::scene
{
    namespace
    {
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
            if (dynamic_cast<const CanvasComponent *>(&component))
                return "CanvasComponent";
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
            if (componentType == "SkeletonAttachmentComponent")
                return std::make_unique<SkeletonAttachmentComponent>();
            if (componentType == "CameraComponent")
                return std::make_unique<CameraComponent>(new render::Camera(render::CameraConfig{}));
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
            if (componentType == "CanvasComponent")
                return std::make_unique<CanvasComponent>();
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
                    auto component = CreateComponentForType(typeName);
                    if (!component)
                    {
                        continue;
                    }

                    auto *destinationComponent = destination.AddComponent(component.release());
                    CopyComponent(*destinationComponent, *sourceComponent);
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
                sscanf_s(std::string(value).c_str(), "%f,%f,%f", &parsed.x, &parsed.y, &parsed.z);
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
        auto prefabScene = LoadPrefabScene(prefabReference, errorMessage);
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

        auto *instanceRoot = CloneEntityTreeIntoScene(scene, *roots.front(), prefabReference, parent, true);
        if (instanceRoot)
        {
            RemapClonedScriptEntityReferences(*roots.front(), *instanceRoot);
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
