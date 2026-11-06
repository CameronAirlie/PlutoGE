#include "PlutoGE/ui/hierarchy/HierarchyExpansion.h"

#include "PlutoGE/scene/Entity.h"

namespace PlutoGE::ui
{
    void HierarchyExpansion::SetExpanded(ID id, bool expanded)
    {
        const bool changed = expanded ? m_expanded.insert(id).second : m_expanded.erase(id) > 0;
        if (changed) ++m_revision;
    }

    void HierarchyExpansion::SetSubtreeExpanded(const scene::Entity &entity, bool expanded)
    {
        if (entity.GetChildren().empty())
        {
            return;
        }
        SetExpanded(entity.GetID(), expanded);
        for (const auto *child : entity.GetChildren())
        {
            if (child)
            {
                SetSubtreeExpanded(*child, expanded);
            }
        }
    }

    void HierarchyExpansion::ExpandAncestors(const scene::Entity &entity)
    {
        for (const auto *ancestor = entity.GetParent(); ancestor; ancestor = ancestor->GetParent())
        {
            if (m_expanded.insert(ancestor->GetID()).second) ++m_revision;
        }
    }
}
