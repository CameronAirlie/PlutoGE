#pragma once

#include <functional>

namespace PlutoGE::assets
{
    class Project;
}

namespace PlutoGE::scene
{
    class DetailGrassComponent;
    class Entity;
}

// The detail grass inspector (docs/DETAIL_GRASS.md): the layer list and the selected layer's settings. Registered
// with the other built-in component inspectors; drawn through a host so it runs without the editor shell (tests).
namespace PlutoGE::ui
{
    // What the inspector needs from the editor.
    class DetailGrassInspectorHost
    {
    public:
        virtual ~DetailGrassInspectorHost() = default;
        // Runs `edit` (a change to the layer list, such as adding or removing a layer) as one labelled undo step.
        virtual void EditStep(const char *label, const std::function<void()> &edit) = 0;
        // Reports a change a control already made; consecutive reports of a drag merge into one step.
        virtual void Changed() = 0;
        // For the material picker; may be null.
        [[nodiscard]] virtual const assets::Project *Project() const = 0;
    };

    // Draws the inspector for `grass` on `entity`, recording prefab overrides on the entity for whatever it changes.
    void DrawDetailGrassInspector(scene::DetailGrassComponent &grass, scene::Entity &entity, DetailGrassInspectorHost &host);
}
