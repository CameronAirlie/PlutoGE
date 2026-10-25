#include "PlutoGE/ui/hierarchy/HierarchyDropPlacement.h"

#include "PlutoGE/scene/Entity.h"

namespace PlutoGE::ui
{
    HierarchyDropZone ClassifyHierarchyDrop(float rowTop, float rowBottom, float pointerY)
    {
        const float edge = (rowBottom - rowTop) * kHierarchyDropEdgeFraction;
        if (pointerY < rowTop + edge)
        {
            return HierarchyDropZone::Before;
        }
        if (pointerY > rowBottom - edge)
        {
            return HierarchyDropZone::After;
        }
        return HierarchyDropZone::Inside;
    }

    HierarchyPlacement ResolveHierarchyDrop(scene::Entity &target, HierarchyDropZone zone, bool targetExpanded)
    {
        switch (zone)
        {
        case HierarchyDropZone::Inside:
            return {.parent = &target, .index = target.GetChildren().size()};
        case HierarchyDropZone::Before:
            return {.parent = target.GetParent(), .index = target.GetSiblingIndex()};
        case HierarchyDropZone::After:
            if (targetExpanded && !target.GetChildren().empty())
            {
                return {.parent = &target, .index = 0};
            }
            return {.parent = target.GetParent(), .index = target.GetSiblingIndex() + 1};
        }
        return {.parent = target.GetParent(), .index = target.GetSiblingIndex()};
    }

    bool MoveEntityTo(scene::Entity &entity, const HierarchyPlacement &placement)
    {
        auto *parent = placement.parent;
        if (parent == &entity || (parent && parent->IsDescendantOf(&entity)))
        {
            return false;
        }

        // The placement index ignores the entity itself; compensate when it
        // currently sits earlier in the same sibling list.
        std::size_t index = placement.index;
        if (entity.GetParent() == parent && entity.GetSiblingIndex() < index)
        {
            --index;
        }

        if (entity.GetParent() != parent)
        {
            entity.SetParent(parent);
            if (entity.GetParent() != parent)
            {
                return false;
            }
        }
        entity.SetSiblingIndex(index);
        return true;
    }

    std::size_t MoveEntitiesRelativeTo(std::span<scene::Entity *const> entities, scene::Entity &target,
                                       HierarchyDropZone zone, bool targetExpanded)
    {
        std::size_t moved = 0;
        scene::Entity *anchor = &target;
        for (auto *entity : entities)
        {
            if (!entity || entity == &target || target.IsDescendantOf(entity))
            {
                continue;
            }
            if (!MoveEntityTo(*entity, ResolveHierarchyDrop(*anchor, zone, targetExpanded)))
            {
                continue;
            }
            ++moved;
            // Later entities follow the previous one so the block keeps its order.
            if (zone != HierarchyDropZone::Inside)
            {
                anchor = entity;
                zone = HierarchyDropZone::After;
                targetExpanded = false;
            }
        }
        return moved;
    }
}
