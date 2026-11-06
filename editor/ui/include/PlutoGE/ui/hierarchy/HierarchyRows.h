#pragma once

#include "PlutoGE/ui/hierarchy/HierarchyNavigation.h"

#include <vector>

namespace PlutoGE::scene { class Scene; }

namespace PlutoGE::ui
{
    class HierarchyExpansion;
    class HierarchyFilter;

    // Every row the tree shows, in display order: visible entities whose ancestors are all open. While filtering, rows
    // are open regardless of the stored expansion, which stays untouched for when the filter is cleared.
    [[nodiscard]] std::vector<HierarchyRow> BuildHierarchyRows(const scene::Scene &scene,
                                                               const HierarchyFilter &filter,
                                                               const HierarchyExpansion &expansion);
}
