#include "PlutoGE/ui/PlayModeChanges.h"

#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"

#include <algorithm>
#include <charconv>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace PlutoGE::ui
{
    namespace
    {
        using scene::Property;
        using scene::PropertyType;

        std::string FormatVector(glm::vec3 value)
        {
            // Match scene persistence precision so a restored baseline compares equal.
            return std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z);
        }

        glm::vec3 ParseVector(const std::string &text)
        {
            std::istringstream input(text);
            input.imbue(std::locale::classic());
            glm::vec3 result;
            char separator1 = 0, separator2 = 0;
            if (!(input >> result.x >> separator1 >> result.y >> separator2 >> result.z) ||
                separator1 != ',' || separator2 != ',' || !input.eof())
                throw std::runtime_error("Invalid retained transform.");
            return result;
        }

        const Property *FindProperty(const std::vector<Property> &properties, const Property &key)
        {
            const auto it = std::find_if(properties.begin(), properties.end(), [&](const Property &property) {
                return property.name == key.name && property.type == key.type;
            });
            return it == properties.end() ? nullptr : &*it;
        }

        bool SameSchema(const std::vector<Property> &a, const std::vector<Property> &b)
        {
            return a.size() == b.size() && std::all_of(a.begin(), a.end(), [&](const Property &property) {
                const auto *other = FindProperty(b, property);
                return other && property.enumOptions == other->enumOptions;
            });
        }

        bool ValidReference(const Property &property, const PlayModeChanges::Snapshot &baseline)
        {
            if (property.type != PropertyType::Entity)
                return true;
            // Component references use the same entity ID representation.
            scene::EntityID id = 0;
            const auto [end, error] = std::from_chars(property.value.data(),
                property.value.data() + property.value.size(), id);
            return error == std::errc{} && end == property.value.data() + property.value.size() &&
                   (id == 0 || baseline.contains(id));
        }

        void CaptureEntity(const scene::Entity &entity, PlayModeChanges::Snapshot &snapshot)
        {
            PlayModeChanges::EntityState state;
            state.parent = entity.GetParent() ? entity.GetParent()->GetID() : 0;
            state.name = entity.GetName();
            state.prefabSource = entity.GetPrefabSource();
            state.prefabEntity = entity.GetPrefabEntityID();
            state.componentRevision = entity.GetComponentRevision();
            state.properties = {
                {"Name", PropertyType::String, entity.GetName(), {}},
                {"Active", PropertyType::Bool, entity.IsSelfActive() ? "true" : "false", {}},
                {"Position", PropertyType::Vec3, FormatVector(entity.GetPosition()), {}},
                {"Rotation", PropertyType::Vec3, FormatVector(entity.GetRotation()), {}},
                {"Scale", PropertyType::Vec3, FormatVector(entity.GetScale()), {}}};
            for (const auto &bucket : entity.GetComponentBuckets())
            {
                for (std::size_t index = 0; index < bucket.size(); ++index)
                {
                    const auto *component = bucket[index];
                    const auto name = scene::SceneSerializer::GetComponentTypeName(*component);
                    if (!name.empty())
                        state.components.push_back({component->GetTypeID(), index, name,
                                                    component->IsEnabled(), component->Serialize()});
                }
            }
            snapshot.emplace(entity.GetID(), std::move(state));
            for (const auto *child : entity.GetChildren())
                CaptureEntity(*child, snapshot);
        }
    }

    PlayModeChanges::Snapshot PlayModeChanges::Capture(const scene::Scene &scene)
    {
        Snapshot result;
        for (const auto *root : scene.GetRootEntities())
            CaptureEntity(*root, result);
        return result;
    }

    std::vector<PlayModeChanges::Change> PlayModeChanges::Compare(const Snapshot &before, const Snapshot &after)
    {
        std::vector<Change> changes;
        for (const auto &[id, original] : before)
        {
            const auto currentIt = after.find(id);
            if (currentIt == after.end())
                continue; // Runtime deletion is not an authoring edit.
            const auto &current = currentIt->second;
            if (original.prefabSource != current.prefabSource || original.prefabEntity != current.prefabEntity)
                continue;

            const auto append = [&](const Property &oldProperty, const Property &newProperty,
                                    const ComponentState *component, std::string reason = {}) {
                if (oldProperty.value == newProperty.value)
                    return;
                if (!ValidReference(newProperty, before))
                    reason = "Reference targets an entity created during play.";
                changes.push_back({id, original.name, component ? component->name : std::string{},
                    component ? component->type : 0, component ? component->index : 0,
                    oldProperty, newProperty, std::move(reason), false});
            };

            for (const auto &property : original.properties)
            {
                const auto *changed = FindProperty(current.properties, property);
                if (!changed)
                    continue;
                const bool transform = property.type == PropertyType::Vec3;
                append(property, *changed, nullptr, transform && original.parent != current.parent
                    ? "Local transform belongs to a different parent during play." : "");
            }

            // Ordinals identify repeated components only while the layout is
            // unchanged. A removal and replacement also increments the revision.
            if (original.componentRevision != current.componentRevision ||
                original.components.size() != current.components.size())
                continue;
            for (std::size_t index = 0; index < original.components.size(); ++index)
            {
                const auto &oldComponent = original.components[index];
                const auto &newComponent = current.components[index];
                // Existing prefab paths address the first component of a type.
                if (!original.prefabSource.empty() && oldComponent.index != 0)
                    continue;
                if (oldComponent.type != newComponent.type || oldComponent.index != newComponent.index ||
                    !SameSchema(oldComponent.properties, newComponent.properties))
                    continue;
                // A script class change is structural even if field names match.
                const Property sourceKey{"Source", PropertyType::String, {}, {}};
                const auto *oldSource = FindProperty(oldComponent.properties, sourceKey);
                const auto *newSource = FindProperty(newComponent.properties, sourceKey);
                if (oldComponent.name == "ScriptComponent" && oldSource && newSource &&
                    oldSource->value != newSource->value)
                    continue;
                append({"$Enabled", PropertyType::Bool, oldComponent.enabled ? "true" : "false", {}},
                       {"$Enabled", PropertyType::Bool, newComponent.enabled ? "true" : "false", {}}, &oldComponent);
                for (const auto &property : oldComponent.properties)
                    append(property, *FindProperty(newComponent.properties, property), &oldComponent);
            }
        }
        return changes;
    }

    bool PlayModeChanges::Apply(scene::Scene &restoredScene, const std::vector<Change> &changes,
                               std::string &errorMessage)
    {
        errorMessage.clear();
        try
        {
            const auto restored = Capture(restoredScene);
            using ComponentKey = std::tuple<scene::EntityID, scene::ComponentTypeID, std::size_t>;
            std::map<ComponentKey, std::vector<Property>> componentEdits;
            // Validate all values against the restored authoring state first.
            for (const auto &change : changes)
            {
                if (!change.selected)
                    continue;
                if (!change.unavailableReason.empty())
                    throw std::runtime_error(change.unavailableReason);
                const auto entityIt = restored.find(change.entity);
                if (entityIt == restored.end() || !ValidReference(change.after, restored))
                    throw std::runtime_error("A retained entity reference is no longer valid.");
                const Property *existing = nullptr;
                Property enabled;
                if (change.componentName.empty())
                    existing = FindProperty(entityIt->second.properties, change.before);
                else
                {
                    const auto &components = entityIt->second.components;
                    const auto componentIt = std::find_if(components.begin(), components.end(), [&](const ComponentState &c) {
                        return c.type == change.componentType && c.index == change.componentIndex && c.name == change.componentName;
                    });
                    if (componentIt == components.end())
                        throw std::runtime_error("A retained component no longer exists.");
                    if (change.before.name == "$Enabled")
                    {
                        enabled = {"$Enabled", PropertyType::Bool, componentIt->enabled ? "true" : "false", {}};
                        existing = &enabled;
                    }
                    else
                    {
                        existing = FindProperty(componentIt->properties, change.before);
                        auto editIt = componentEdits.try_emplace(
                            ComponentKey{change.entity, change.componentType, change.componentIndex}, componentIt->properties).first;
                        for (auto &property : editIt->second)
                            if (property.name == change.before.name && property.type == change.before.type)
                                property = change.after;
                    }
                }
                if (!existing || existing->value != change.before.value)
                    throw std::runtime_error("The restored value changed: " + change.entityName + "/" + change.before.name);
            }

            for (const auto &[key, properties] : componentEdits)
            {
                const auto &[entityId, type, index] = key;
                restoredScene.FindEntityByID(entityId)->GetComponentBuckets()[type][index]->Deserialize(properties);
            }
            for (const auto &change : changes)
            {
                if (!change.selected)
                    continue;
                auto *entity = restoredScene.FindEntityByID(change.entity);
                std::string overridePath = change.after.name;
                if (change.componentName.empty())
                {
                    if (change.after.name == "Name") entity->SetName(change.after.value);
                    else if (change.after.name == "Active") entity->SetActive(change.after.value == "true");
                    else if (change.after.name == "Position") entity->SetPosition(ParseVector(change.after.value));
                    else if (change.after.name == "Rotation") entity->SetRotation(ParseVector(change.after.value));
                    else if (change.after.name == "Scale") entity->SetScale(ParseVector(change.after.value));
                    if (change.after.type == PropertyType::Vec3)
                        overridePath = "Transform." + change.after.name;
                }
                else
                {
                    if (change.after.name == "$Enabled")
                        entity->GetComponentBuckets()[change.componentType][change.componentIndex]->SetEnabled(change.after.value == "true");
                    overridePath = "Component:" + change.componentName + ":" +
                                   (change.after.name == "$Enabled" ? "Enabled" : change.after.name);
                }
                entity->AddPrefabOverride(std::move(overridePath));
            }
            return true;
        }
        catch (const std::exception &error)
        {
            errorMessage = error.what();
            return false;
        }
    }
}
