#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace PlutoGE::assets
{
    class Project;
}

namespace PlutoGE::render
{
    class Mesh;
}

namespace PlutoGE::scene
{
    class Entity;
    class OutfitComponent;
    class WearableComponent;
}

// Inspectors for clothing (PlutoGE docs/SKELETAL_IK.md, "Clothing and outfits"): the Outfit's list of worn items with
// each one's state and fit, and a Wearable's slot, the body skin it hides and a fit check against a chosen body.
// Registered with the other built-in component inspectors; drawn through a host so they run without the shell (tests).
namespace PlutoGE::ui
{
    class OutfitInspectorHost
    {
    public:
        virtual ~OutfitInspectorHost() = default;
        // Runs `edit` as one labelled undo step.
        virtual void EditStep(const char *label, const std::function<void()> &edit) = 0;
        // For the asset pickers; may be null.
        [[nodiscard]] virtual const assets::Project *Project() const = 0;
        // A mesh for the wearable's fit check; null when it cannot be loaded.
        [[nodiscard]] virtual const render::Mesh *LoadMesh(const std::string &reference) = 0;
        // The body a wearable on `entity` is checked against (editor state, not saved).
        [[nodiscard]] virtual std::string &FitCheckBody(std::uint32_t entity) = 0;
    };

    void DrawOutfitInspector(scene::OutfitComponent &outfit, scene::Entity &entity, OutfitInspectorHost &host);
    void DrawWearableInspector(scene::WearableComponent &wearable, scene::Entity &entity, OutfitInspectorHost &host);
}
