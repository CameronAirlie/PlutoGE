#pragma once

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/FoliageComponent.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace PlutoGE::scene
{
    class Scene;
}

// Which foliage types in a scene use a .plutofoliage asset, and the list showing them in the Foliage Editor.
namespace PlutoGE::ui
{
    struct FoliageTypeUser
    {
        scene::EntityID entity = 0;
        std::string entityName;
        std::size_t typeIndex = 0;
        std::string typeName;
        std::size_t instanceCount = 0;
    };

    // Every type in `scene` linked to `reference`, in hierarchy order.
    [[nodiscard]] std::vector<FoliageTypeUser> FindFoliageTypeUsers(scene::Scene &scene, const std::string &reference);
    // The first such type, or null.
    [[nodiscard]] const scene::FoliageType *FindFoliageTypeLinkedTo(scene::Scene &scene, const std::string &reference);

    struct FoliageUsageViewOptions
    {
        // The selected entity that can take a new type from this asset; empty when there is none.
        std::string addTargetName;
        // False while the game runs: the list still shows, but it offers no edits.
        bool editable = true;
    };

    struct FoliageUsageViewResult
    {
        std::optional<std::size_t> show;  // Index of the row whose type to show in the inspector.
        std::optional<std::size_t> paint; // Index of the row whose type to paint.
        bool addToTarget = false;
    };

    [[nodiscard]] FoliageUsageViewResult DrawFoliageUsageView(const std::vector<FoliageTypeUser> &users, const FoliageUsageViewOptions &options);
}
