#include "PlutoGE/ui/hierarchy/HierarchyExpansion.h"

#include "PlutoGE/scene/Entity.h"

namespace PlutoGE::ui
{
    void HierarchyExpansion::SetExpanded(ID id, bool expanded)
    {
        if (expanded)
        {
            m_expanded.insert(id);
        }
        else
        {
            m_expanded.erase(id);
        }
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
            m_expanded.insert(ancestor->GetID());
        }
    }
}
