#pragma once

#include "PlutoGE/scene/components/Component.h"

#include <optional>
#include <string>
#include <vector>

namespace PlutoGE::scene
{
    class Entity;
}

namespace PlutoGE::ui
{
    // A component's values, copied for pasting onto another component of the same type or as a new component.
    struct ComponentValues
    {
        std::string typeName; // Registered type name ("LightComponent").
        std::string displayName;
        std::vector<scene::Property> properties;
    };

    // Empty for component types the scene format cannot store.
    [[nodiscard]] std::optional<ComponentValues> CopyComponentValues(const scene::Component &component);
    [[nodiscard]] bool CanPasteComponentValues(const ComponentValues &values, const scene::Component &target);
    // Applies the values and returns the names of the properties that changed, for prefab overrides.
    std::vector<std::string> PasteComponentValues(const ComponentValues &values, scene::Component &target);
    // Adds a new component built from the values. Null when the type cannot be created.
    scene::Component *AddComponentFromValues(const ComponentValues &values, scene::Entity &entity);

    // Reset restores a freshly created component's values. Scripts are excluded: their defaults come from the
    // script class, and resetting would also clear which script runs.
    [[nodiscard]] bool CanResetComponent(const scene::Component &component);
    std::vector<std::string> ResetComponentValues(scene::Component &component);

    // Position among components of the same type on its entity, and how many there are.
    struct ComponentSlot
    {
        std::size_t index = 0;
        std::size_t count = 0;
    };
    [[nodiscard]] ComponentSlot GetComponentSlot(const scene::Component &component);
}
