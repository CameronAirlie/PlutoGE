#pragma once

#include "PlutoGE/scene/Entity.h"

#include <map>
#include <string>
#include <vector>

namespace PlutoGE::scene { class Scene; }

namespace PlutoGE::ui
{
    // Value-only snapshots. No runtime pointers survive a play/stop transition.
    class PlayModeChanges
    {
    public:
        struct ComponentState
        {
            scene::ComponentTypeID type = 0;
            std::size_t index = 0;
            std::string name;
            bool enabled = true;
            std::vector<scene::Property> properties;
        };

        struct EntityState
        {
            scene::EntityID parent = 0;
            std::string name;
            std::string prefabSource;
            scene::EntityID prefabEntity = 0;
            uint64_t componentRevision = 0;
            std::vector<scene::Property> properties;
            std::vector<ComponentState> components;
        };

        using Snapshot = std::map<scene::EntityID, EntityState>;

        struct Change
        {
            scene::EntityID entity = 0;
            std::string entityName;
            // Empty componentName denotes an entity property.
            std::string componentName;
            scene::ComponentTypeID componentType = 0;
            std::size_t componentIndex = 0;
            scene::Property before;
            scene::Property after;
            std::string unavailableReason;
            bool selected = false;
        };

        static Snapshot Capture(const scene::Scene &scene);
        static std::vector<Change> Compare(const Snapshot &before, const Snapshot &after);

        // Apply to a detached, restored scene. The caller discards that scene on
        // failure and publishes it only after successful serialization/validation.
        static bool Apply(scene::Scene &restoredScene, const std::vector<Change> &changes,
                          std::string &errorMessage);
    };
}
