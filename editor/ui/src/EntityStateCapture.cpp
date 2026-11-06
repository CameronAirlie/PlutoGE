#include "PlutoGE/ui/EntityStateCapture.h"

#include "PlutoGE/scene/ComponentRegistry.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/MeshComponent.h"

#include <algorithm>
#include <exception>
#include <memory>
#include <stdexcept>

namespace PlutoGE::ui
{
    namespace
    {
        bool SameProperty(const scene::Property &a, const scene::Property &b)
        {
            if (a.name != b.name || a.type != b.type || a.enumOptions != b.enumOptions) return false;
            if (a.sharedValue && a.sharedValue == b.sharedValue) return true;
            return a.Text() == b.Text();
        }

        bool SameProperties(const std::vector<scene::Property> &a, const std::vector<scene::Property> &b)
        {
            return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), SameProperty);
        }

        bool SameComponent(const ComponentState &a, const ComponentState &b)
        {
            return a.typeId == b.typeId && a.enabled == b.enabled && SameProperties(a.properties, b.properties);
        }

        bool SameEntityState(const EntityState &a, const EntityState &b)
        {
            return a.parentId == b.parentId && a.siblingIndex == b.siblingIndex && a.name == b.name &&
                   a.active == b.active && a.position == b.position && a.rotation == b.rotation && a.scale == b.scale &&
                   a.linearCorrection == b.linearCorrection && a.tags == b.tags && a.prefabSource == b.prefabSource &&
                   a.prefabEntityId == b.prefabEntityId && a.prefabRoot == b.prefabRoot &&
                   a.prefabOverrides == b.prefabOverrides && a.components.size() == b.components.size() &&
                   std::equal(a.components.begin(), a.components.end(), b.components.begin(), SameComponent);
        }

        // Component layout or heavy data differing is what a RevisionOnly capture cannot restore.
        bool HeavyDataOrLayoutDiffers(const EntityState &a, const EntityState &b)
        {
            if (a.components.size() != b.components.size()) return true;
            for (std::size_t index = 0; index < a.components.size(); ++index)
                if (a.components[index].typeId != b.components[index].typeId ||
                    a.components[index].heavyRevision != b.components[index].heavyRevision)
                    return true;
            return false;
        }

        std::vector<scene::Property> SerializeFor(const scene::Component &component, HeavyDataCapture heavy)
        {
            return heavy == HeavyDataCapture::Full ? component.SerializeShared() : component.SerializeForInspector();
        }

        // Applies `target` to a live component. Returns false if the component does not end up matching, which a
        // Deserialize that merges rather than replaces can cause (a shorter list keeping its old extra entries).
        bool ApplyInPlace(scene::Component &component, const ComponentState &target, HeavyDataCapture heavy)
        {
            const auto current = SerializeFor(component, heavy);
            if (!SameProperties(current, target.properties))
            {
                // Unchanged heavy data is left where it is, since re-reading it costs far more than the edit.
                std::vector<scene::Property> changed;
                changed.reserve(target.properties.size());
                for (const auto &property : target.properties)
                {
                    if (property.sharedValue)
                    {
                        const auto same = std::find_if(current.begin(), current.end(), [&](const scene::Property &candidate)
                                                       { return candidate.name == property.name && SameProperty(candidate, property); });
                        if (same != current.end()) continue;
                    }
                    changed.push_back(property);
                }
                component.DeserializeKeepingHeavyData(changed);
            }
            component.SetEnabled(target.enabled);
            return SameProperties(SerializeFor(component, heavy), target.properties);
        }

        void AddFreshComponent(scene::Entity &entity, const ComponentState &target, const std::vector<scene::Property> &keptHeavy)
        {
            auto created = scene::ComponentRegistry::Get().Create(target.typeName);
            if (!created) throw std::runtime_error("Component type is no longer registered: " + target.typeName);
            auto *component = entity.AddComponent(created.release());
            auto properties = target.properties;
            properties.insert(properties.end(), keptHeavy.begin(), keptHeavy.end());
            component->Deserialize(properties);
            component->SetEnabled(target.enabled);
        }

        std::vector<scene::Property> HeavyProperties(const scene::Component &component)
        {
            auto properties = component.SerializeShared();
            std::erase_if(properties, [](const scene::Property &property) { return !property.sharedValue; });
            return properties;
        }

        // Components are positional within their bucket, so matching by position restores an exact layout without
        // needing to insert into the middle: a removed middle component comes back as its successor's new values.
        void ApplyBucket(scene::Entity &entity, scene::ComponentTypeID typeId, const std::vector<const ComponentState *> &targets,
                         HeavyDataCapture heavy)
        {
            const auto &buckets = entity.GetComponentBuckets();
            std::vector<scene::Component *> current = typeId < buckets.size() ? buckets[typeId] : std::vector<scene::Component *>{};
            const std::size_t shared = (std::min)(current.size(), targets.size());
            std::size_t next = shared;
            std::vector<std::vector<scene::Property>> keptHeavy(targets.size());
            for (std::size_t index = 0; index < shared; ++index)
            {
                if (ApplyInPlace(*current[index], *targets[index], heavy)) continue;
                // Rebuild from here on, to keep bucket order. A RevisionOnly target has no heavy data of its own.
                if (heavy == HeavyDataCapture::RevisionOnly)
                    for (std::size_t later = index; later < shared; ++later)
                        keptHeavy[later] = HeavyProperties(*current[later]);
                next = index;
                break;
            }
            for (std::size_t index = current.size(); index > next; --index)
                entity.RemoveComponent(current[index - 1]);
            for (std::size_t index = next; index < targets.size(); ++index)
                AddFreshComponent(entity, *targets[index], keptHeavy[index]);
        }

        void ApplyComponents(scene::Entity &entity, const EntityState &target)
        {
            std::map<scene::ComponentTypeID, std::vector<const ComponentState *>> wanted;
            for (const auto &component : target.components) wanted[component.typeId].push_back(&component);
            // Types with no registered name are invisible to the scene format, and so to history.
            const auto &buckets = entity.GetComponentBuckets();
            for (scene::ComponentTypeID type = 0; type < buckets.size(); ++type)
                if (!buckets[type].empty() && scene::ComponentRegistry::Get().Find(*buckets[type].front()))
                    wanted.try_emplace(type);
            for (const auto &[type, targets] : wanted)
                ApplyBucket(entity, type, targets, target.heavy);
        }

        void ApplyEntityFields(scene::Entity &entity, const EntityState &target)
        {
            if (entity.GetName() != target.name) entity.SetName(target.name);
            if (entity.GetTags() != target.tags) entity.SetTags(target.tags);
            if (entity.IsSelfActive() != target.active) entity.SetActive(target.active);
            if (entity.GetPosition() != target.position || entity.GetRotation() != target.rotation || entity.GetScale() != target.scale)
                entity.SetLocalTransform({target.position, target.rotation, target.scale});
            if (entity.GetLocalTransformCorrection() != target.linearCorrection)
                entity.SetLocalTransformCorrection(target.linearCorrection);
            if (entity.GetPrefabSource() != target.prefabSource || entity.GetPrefabEntityID() != target.prefabEntityId ||
                entity.IsPrefabInstanceRoot() != target.prefabRoot)
                entity.SetPrefabLink(target.prefabSource, target.prefabEntityId, target.prefabRoot);
            if (entity.GetPrefabOverrides() != target.prefabOverrides) entity.SetPrefabOverrides(target.prefabOverrides);
        }

        std::vector<const scene::Entity *> SubtreeOf(const scene::Entity &root)
        {
            std::vector<const scene::Entity *> entities{&root};
            for (std::size_t index = 0; index < entities.size(); ++index)
                for (const auto *child : entities[index]->GetChildren())
                    entities.push_back(child);
            return entities;
        }

        // Parents before children, in the hierarchy the targets describe.
        std::vector<const EntityState *> OrderParentsFirst(const std::vector<const EntityState *> &targets)
        {
            std::map<scene::EntityID, const EntityState *> byId;
            for (const auto *target : targets) byId.emplace(target->id, target);
            const auto depthOf = [&byId](const EntityState *state)
            {
                std::size_t depth = 0;
                for (auto parent = byId.find(state->parentId); parent != byId.end() && depth <= byId.size();
                     parent = byId.find(parent->second->parentId))
                    ++depth;
                return depth;
            };
            std::vector<std::pair<std::size_t, const EntityState *>> ordered;
            ordered.reserve(targets.size());
            for (const auto *target : targets) ordered.emplace_back(depthOf(target), target);
            std::stable_sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
            std::vector<const EntityState *> result;
            result.reserve(ordered.size());
            for (const auto &[depth, state] : ordered) result.push_back(state);
            return result;
        }
    }

    std::size_t EntityState::ByteSize() const
    {
        std::size_t bytes = sizeof(EntityState) + name.size() + prefabSource.size();
        for (const auto &tag : tags) bytes += tag.size();
        for (const auto &path : prefabOverrides) bytes += path.size();
        for (const auto &component : components)
        {
            bytes += sizeof(ComponentState) + component.typeName.size();
            for (const auto &property : component.properties)
                bytes += sizeof(scene::Property) + property.name.size() + property.Text().size();
        }
        return bytes;
    }

    std::size_t SceneEntityDelta::ByteSize() const
    {
        std::size_t bytes = sizeof(SceneEntityDelta);
        for (const auto &change : changes)
            bytes += sizeof(EntityStateChange) + (change.before ? change.before->ByteSize() : 0) + (change.after ? change.after->ByteSize() : 0);
        return bytes;
    }

    bool CanCaptureEntityState(const scene::Scene &scene, const scene::Entity &entity)
    {
        // A mesh reading from a retained generation resolves its references through that reader, which states cannot carry.
        for (const auto *mesh : entity.GetComponents<scene::MeshComponent>())
            if (mesh->GetRetainedAssetReader()) return false;
        const auto &instances = scene.GetStaticModelInstances();
        if (instances.empty()) return true;
        for (const auto *current = &entity; current; current = current->GetParent())
            if (instances.contains(current->GetID())) return false;
        const auto id = entity.GetID();
        for (const auto &[root, instance] : instances)
        {
            for (const auto &node : instance.state.nodeEntities)
                if (node.sceneEntityId == id) return false;
            for (const auto binding : instance.state.bindingEntities)
                if (binding == id) return false;
        }
        return true;
    }

    std::optional<EntityState> CaptureEntityState(const scene::Entity &entity, HeavyDataCapture heavy)
    {
        try
        {
            EntityState state;
            state.id = entity.GetID();
            state.parentId = entity.GetParent() ? entity.GetParent()->GetID() : 0;
            state.siblingIndex = entity.GetSiblingIndex();
            state.name = entity.GetName();
            state.active = entity.IsSelfActive();
            state.position = entity.GetPosition();
            state.rotation = entity.GetRotation();
            state.scale = entity.GetScale();
            state.linearCorrection = entity.GetLocalTransformCorrection();
            state.tags = entity.GetTags();
            state.prefabSource = entity.GetPrefabSource();
            state.prefabEntityId = entity.GetPrefabEntityID();
            state.prefabRoot = entity.IsPrefabInstanceRoot();
            state.prefabOverrides = entity.GetPrefabOverrides();
            state.heavy = heavy;
            for (const auto &bucket : entity.GetComponentBuckets())
                for (const auto *component : bucket)
                {
                    const auto *info = component ? scene::ComponentRegistry::Get().Find(*component) : nullptr;
                    if (!info || info->typeName.empty()) continue;
                    state.components.push_back(ComponentState{
                        .typeId = component->GetTypeID(),
                        .typeName = info->typeName,
                        .enabled = component->IsEnabled(),
                        .heavyRevision = component->GetHeavyDataRevision(),
                        .properties = SerializeFor(*component, heavy),
                    });
                }
            return state;
        }
        catch (const std::exception &)
        {
            return std::nullopt;
        }
    }

    std::optional<EntityStates> CaptureEntityStates(const scene::Scene &scene, std::span<const scene::EntityID> ids,
                                                    bool includeDescendants, HeavyDataCapture heavy)
    {
        EntityStates states;
        const auto add = [&](const scene::Entity &entity)
        {
            if (states.contains(entity.GetID())) return true;
            if (!CanCaptureEntityState(scene, entity)) return false;
            auto state = CaptureEntityState(entity, heavy);
            if (!state) return false;
            states.emplace(entity.GetID(), std::move(*state));
            return true;
        };
        for (const auto id : ids)
        {
            const auto *entity = scene.FindEntityByID(id);
            if (!entity) continue;
            if (!includeDescendants)
            {
                if (!add(*entity)) return std::nullopt;
                continue;
            }
            for (const auto *member : SubtreeOf(*entity))
                if (!add(*member)) return std::nullopt;
        }
        return states;
    }

    SceneEntityDelta DiffEntityStates(const EntityStates &before, const EntityStates &after)
    {
        SceneEntityDelta delta;
        const auto note = [&](const EntityState *a, const EntityState *b)
        {
            const bool revisionOnly = (a && a->heavy == HeavyDataCapture::RevisionOnly) || (b && b->heavy == HeavyDataCapture::RevisionOnly);
            if (revisionOnly && (!a || !b || HeavyDataOrLayoutDiffers(*a, *b))) delta.requiresSnapshot = true;
            delta.changes.push_back({(a ? a : b)->id, a ? std::optional(*a) : std::nullopt, b ? std::optional(*b) : std::nullopt});
        };
        for (const auto &[id, state] : before)
        {
            const auto found = after.find(id);
            if (found == after.end()) note(&state, nullptr);
            else if (!SameEntityState(state, found->second) ||
                     (state.heavy == HeavyDataCapture::RevisionOnly && HeavyDataOrLayoutDiffers(state, found->second)))
                note(&state, &found->second);
        }
        for (const auto &[id, state] : after)
            if (!before.contains(id)) note(nullptr, &state);
        return delta;
    }

    bool ApplyEntityDelta(scene::Scene &scene, const SceneEntityDelta &delta, EntityDeltaDirection direction, std::string &error)
    {
        try
        {
            std::vector<const EntityState *> targets;
            std::vector<scene::EntityID> toRemove;
            for (const auto &change : delta.changes)
            {
                const auto &target = direction == EntityDeltaDirection::Undo ? change.before : change.after;
                if (target) targets.push_back(&*target);
                else toRemove.push_back(change.id);
            }
            const auto ordered = OrderParentsFirst(targets);

            // Everything has to exist before anything is parented to it.
            for (const auto *target : ordered)
                if (!scene.FindEntityByID(target->id))
                    scene.AddEntity(std::make_unique<scene::Entity>(target->id, scene::EntityConfig{.name = target->name}));

            for (const auto *target : ordered)
            {
                auto *entity = scene.FindEntityByID(target->id);
                auto *parent = target->parentId ? scene.FindEntityByID(target->parentId) : nullptr;
                if (target->parentId && !parent)
                    throw std::runtime_error("Parent of '" + target->name + "' no longer exists.");
                if (entity->GetParent() != parent) entity->SetParent(parent);
            }

            // Survivors have moved out by now, so removing cannot take any of them along.
            for (const auto id : toRemove)
                if (auto *entity = scene.FindEntityByID(id)) scene.RemoveEntity(entity);

            // Ascending within each parent, so each entity lands at its index with its earlier siblings already placed.
            auto bySibling = ordered;
            std::stable_sort(bySibling.begin(), bySibling.end(), [](const EntityState *a, const EntityState *b)
                             { return a->siblingIndex < b->siblingIndex; });
            for (const auto *target : bySibling)
                scene.FindEntityByID(target->id)->SetSiblingIndex(target->siblingIndex);

            for (const auto *target : ordered)
            {
                auto &entity = *scene.FindEntityByID(target->id);
                ApplyEntityFields(entity, *target);
                ApplyComponents(entity, *target);
            }
            return true;
        }
        catch (const std::exception &exception)
        {
            error = exception.what();
            return false;
        }
    }

    SceneStructure CaptureSceneStructure(const scene::Scene &scene)
    {
        SceneStructure structure;
        std::vector<const scene::Entity *> pending(scene.GetRootEntities().begin(), scene.GetRootEntities().end());
        while (!pending.empty())
        {
            const auto *entity = pending.back();
            pending.pop_back();
            structure.emplace(entity->GetID(), entity->GetParent() ? entity->GetParent()->GetID() : 0);
            pending.insert(pending.end(), entity->GetChildren().begin(), entity->GetChildren().end());
        }
        return structure;
    }
}
