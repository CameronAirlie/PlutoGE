#pragma once

#include <cstdint>
#include <span>

namespace PlutoGE::ui
{
    // One visible hierarchy row, in display order.
    struct HierarchyRow
    {
        std::uint32_t id = 0;
        std::uint32_t parentId = 0; // 0 for scene roots.
        bool hasChildren = false;
        bool expanded = false;
    };

    enum class HierarchyNavKey
    {
        Up,
        Down,
        Left,  // Collapse, or move to the parent row.
        Right, // Expand, or move to the first child row.
        Home,
        End,
    };

    // At most one of the fields is set; zero means no change.
    struct HierarchyNavAction
    {
        std::uint32_t select = 0;
        std::uint32_t expand = 0;
        std::uint32_t collapse = 0;
    };

    // Tree-view keyboard semantics over the visible rows. With no current row
    // (or one that is not visible), Down/Home pick the first row and Up/End
    // the last, so navigation always has somewhere to start.
    [[nodiscard]] HierarchyNavAction NavigateHierarchy(std::span<const HierarchyRow> rows,
                                                       std::uint32_t current, HierarchyNavKey key);
}
