#include "PlutoGE/ui/hierarchy/HierarchyNavigation.h"

#include <algorithm>

namespace PlutoGE::ui
{
    HierarchyNavAction NavigateHierarchy(std::span<const HierarchyRow> rows, std::uint32_t current, HierarchyNavKey key)
    {
        if (rows.empty())
        {
            return {};
        }

        const auto it = std::find_if(rows.begin(), rows.end(), [current](const HierarchyRow &row)
                                     { return row.id == current; });
        if (it == rows.end())
        {
            const bool fromStart = key == HierarchyNavKey::Down || key == HierarchyNavKey::Home;
            return {.select = fromStart ? rows.front().id : rows.back().id};
        }

        const auto index = static_cast<std::size_t>(it - rows.begin());
        const HierarchyRow &row = *it;
        switch (key)
        {
        case HierarchyNavKey::Up:
            return index > 0 ? HierarchyNavAction{.select = rows[index - 1].id} : HierarchyNavAction{};
        case HierarchyNavKey::Down:
            return index + 1 < rows.size() ? HierarchyNavAction{.select = rows[index + 1].id} : HierarchyNavAction{};
        case HierarchyNavKey::Home:
            return index > 0 ? HierarchyNavAction{.select = rows.front().id} : HierarchyNavAction{};
        case HierarchyNavKey::End:
            return index + 1 < rows.size() ? HierarchyNavAction{.select = rows.back().id} : HierarchyNavAction{};
        case HierarchyNavKey::Left:
            if (row.hasChildren && row.expanded)
            {
                return {.collapse = row.id};
            }
            // The parent precedes its children, so it is always visible here.
            return row.parentId != 0 ? HierarchyNavAction{.select = row.parentId} : HierarchyNavAction{};
        case HierarchyNavKey::Right:
            if (!row.hasChildren)
            {
                return {};
            }
            if (!row.expanded)
            {
                return {.expand = row.id};
            }
            // An expanded row with children is followed by its first child.
            return index + 1 < rows.size() ? HierarchyNavAction{.select = rows[index + 1].id} : HierarchyNavAction{};
        }
        return {};
    }
}
