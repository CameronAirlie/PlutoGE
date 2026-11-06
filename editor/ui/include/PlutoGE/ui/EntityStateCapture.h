#pragma once

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/Component.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace PlutoGE::scene { class Scene; }

namespace PlutoGE::ui
{
    // How much of a component's heavy data (Component.h) a capture holds.
    enum class HeavyDataCapture
    {
        // Light properties and the heavy data's revision. Nothing is encoded, so this is cheap enough to take whenever
        // the selection changes, but heavy data cannot be restored from it: see SceneEntityDelta::requiresSnapshot.
        RevisionOnly,
        // Everything, sharing each component's cached heavy text.
        Full,
    };

    struct ComponentState
    {
        scene::ComponentTypeID typeId = 0;
        std::string typeName;
        bool enabled = true;
        std::uint64_t heavyRevision = 0;
        std::vector<scene::Property> properties;
    };

    // What saving an entity would record, except its descendants: restoring this returns the entity to that state.
    struct EntityState
    {
        scene::EntityID id = 0;
        scene::EntityID parentId = 0;
        std::size_t siblingIndex = 0;
        std::string name;
        bool active = true;
        glm::vec3 position{0.0f};
        glm::vec3 rotation{0.0f};
        glm::vec3 scale{1.0f};
        glm::mat4 linearCorrection{1.0f};
        std::vector<std::string> tags;
        std::string prefabSource;
        scene::EntityID prefabEntityId = 0;
        bool prefabRoot = false;
        std::vector<std::string> prefabOverrides;
        HeavyDataCapture heavy = HeavyDataCapture::Full;
        std::vector<ComponentState> components; // In bucket order; position within a bucket is meaningful.

        [[nodiscard]] std::size_t ByteSize() const;
    };

    using EntityStates = std::map<scene::EntityID, EntityState>;

    // Whether an entity's state can be captured and restored by itself. Members of linked model instances cannot:
    // the scene records those through the instance, whose record has to stay consistent with them.
    [[nodiscard]] bool CanCaptureEntityState(const scene::Scene &scene, const scene::Entity &entity);

    // Empty when a component cannot serialize (a script's field schema rejecting its project, for example).
    [[nodiscard]] std::optional<EntityState> CaptureEntityState(const scene::Entity &entity, HeavyDataCapture heavy);

    // Unknown IDs are skipped. Empty on failure, or when `ids` holds an entity CanCaptureEntityState rejects.
    [[nodiscard]] std::optional<EntityStates> CaptureEntityStates(const scene::Scene &scene,
                                                                  std::span<const scene::EntityID> ids,
                                                                  bool includeDescendants,
                                                                  HeavyDataCapture heavy);

    struct EntityStateChange
    {
        scene::EntityID id = 0;
        std::optional<EntityState> before; // Empty: the entity did not exist before the edit.
        std::optional<EntityState> after;  // Empty: the entity no longer exists after the edit.
    };

    struct SceneEntityDelta
    {
        std::vector<EntityStateChange> changes;
        // The states cannot express this edit, because it changed or removed heavy data they did not capture. It needs
        // recording as a whole-scene snapshot.
        bool requiresSnapshot = false;

        [[nodiscard]] bool Empty() const { return changes.empty() && !requiresSnapshot; }
        [[nodiscard]] std::size_t ByteSize() const;
    };

    [[nodiscard]] SceneEntityDelta DiffEntityStates(const EntityStates &before, const EntityStates &after);

    enum class EntityDeltaDirection { Undo, Redo };

    // Changes the scene in place through entity and component setters, so render, physics and shadow invalidation fire
    // as they do for the original edit. Entities keep their IDs. Returns false if an entity could not be restored.
    bool ApplyEntityDelta(scene::Scene &scene, const SceneEntityDelta &delta, EntityDeltaDirection direction,
                          std::string &error);

    // The hierarchy alone, for finding what an edit added, removed or moved. Cheap: no component is serialized.
    using SceneStructure = std::map<scene::EntityID, scene::EntityID>; // Entity to parent; 0 for roots.
    [[nodiscard]] SceneStructure CaptureSceneStructure(const scene::Scene &scene);
}
