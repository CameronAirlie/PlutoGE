#pragma once

#include "PlutoGE/scene/components/Component.h"

#include <functional>
#include <optional>
#include <string>
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

    // A note on one property in the generic editor from a system that drives
    // it, such as a UI style. Locked properties are drawn read-only; actions
    // appear in the property's right-click menu.
    struct PropertyAnnotation
    {
        enum class Kind
        {
            Driven,     // another system sets the value
            Overridden, // the value is set here instead of by that system
        };
        Kind kind = Kind::Driven;
        bool locked = false;
        std::string tooltip;
        struct Action
        {
            std::string label;
            std::function<void()> run;
        };
        std::vector<Action> actions;
    };

    using PropertyAnnotator = std::optional<PropertyAnnotation> (*)(scene::Component &component, ComponentInspectorContext &context,
                                                                    const scene::Property &property);

    void RegisterPropertyAnnotator(PropertyAnnotator annotator);
    // The first annotation any registered annotator gives the property.
    [[nodiscard]] std::optional<PropertyAnnotation> AnnotateProperty(scene::Component &component, ComponentInspectorContext &context,
                                                                     const scene::Property &property);
    // For the property control just drawn: a marker in the left margin, the
    // tooltip on hover and the actions on right-click.
    void DrawPropertyAnnotation(const PropertyAnnotation &annotation);

    // Registration entry points, one per module, run on first lookup.
    void RegisterUIComponentInspectors();
    void RegisterUIStyleInspectors();
}
