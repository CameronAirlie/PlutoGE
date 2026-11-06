#include "PlutoGE/ui/hierarchy/HierarchyRows.h"

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/ui/hierarchy/HierarchyExpansion.h"
#include "PlutoGE/ui/hierarchy/HierarchyFilter.h"

namespace PlutoGE::ui
{
    namespace
    {
        void AppendRows(const scene::Entity &entity, std::uint32_t depth, const HierarchyFilter &filter,
                        const HierarchyExpansion &expansion, std::vector<HierarchyRow> &rows)
        {
            const auto id = entity.GetID();
            if (!filter.IsVisible(id)) return;
            const bool hasChildren = !entity.GetChildren().empty();
            const bool expanded = hasChildren && (filter.IsActive() || expansion.IsExpanded(id));
            rows.push_back({.id = id,
                            .parentId = entity.GetParent() ? entity.GetParent()->GetID() : 0,
                            .hasChildren = hasChildren,
                            .expanded = expanded,
                            .depth = depth});
            if (!expanded) return;
            for (const auto *child : entity.GetChildren())
                if (child) AppendRows(*child, depth + 1, filter, expansion, rows);
        }
    }

    std::vector<HierarchyRow> BuildHierarchyRows(const scene::Scene &scene, const HierarchyFilter &filter, const HierarchyExpansion &expansion)
    {
        std::vector<HierarchyRow> rows;
        for (const auto *root : scene.GetRootEntities())
            if (root) AppendRows(*root, 0, filter, expansion, rows);
        return rows;
    }
}
