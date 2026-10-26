#pragma once

#include "PlutoGE/scene/components/Component.h"

#include <vector>

namespace PlutoGE::scene
{
    class Entity;
}

namespace PlutoGE::ui
{
    class EditorShell;

    struct ComponentInspectorContext
    {
        scene::Entity &entity;
        EditorShell &shell;
    };

    // Draws custom controls for a component above the generic property
    // editor. `properties` arrives holding everything the component
    // serializes; the inspector removes the ones it drew itself (or that do
    // not apply right now) and the generic editor shows the rest. Custom
    // controls mark the scene dirty and record prefab overrides themselves.
    using ComponentInspector = void (*)(scene::Component &component, ComponentInspectorContext &context,
                                        std::vector<scene::Property> &properties);

    void RegisterComponentInspector(scene::ComponentTypeID type, ComponentInspector inspector);
    // Null when the type has no custom inspector.
    [[nodiscard]] ComponentInspector FindComponentInspector(scene::ComponentTypeID type);

    // Registration entry points, one per module, run on first lookup.
    void RegisterUIComponentInspectors();
}
