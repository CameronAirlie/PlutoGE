#pragma once

#include <cstddef>
#include <span>

namespace PlutoGE::scene
{
    class Entity;
}

namespace PlutoGE::ui
{
    // Where a hierarchy drop lands relative to the row under the pointer.
    enum class HierarchyDropZone
    {
        Before, // Sibling placed above the row.
        Inside, // Last child of the row.
        After,  // Sibling placed below the row (first child if it is expanded).
    };

    // Fraction of the row height at each edge that reorders instead of parenting.
    inline constexpr float kHierarchyDropEdgeFraction = 0.25f;

    [[nodiscard]] HierarchyDropZone ClassifyHierarchyDrop(float rowTop, float rowBottom, float pointerY);

    // A parent (null for scene roots) and an index into its children as they
    // would be with the moved entity removed.
    struct HierarchyPlacement
    {
        scene::Entity *parent = nullptr;
        std::size_t index = 0;
    };

    // `targetExpanded` reports whether the row shows its children: dropping
    // below an expanded parent visually lands on its first child slot.
    [[nodiscard]] HierarchyPlacement ResolveHierarchyDrop(scene::Entity &target, HierarchyDropZone zone, bool targetExpanded);

    // False when the move would create a cycle or the scene refuses the parent
    // (for example across streaming sections); the hierarchy is unchanged then.
    bool MoveEntityTo(scene::Entity &entity, const HierarchyPlacement &placement);

    // Moves several entities as a contiguous block in the given order. Entities
    // that are the target, or contain it, are skipped. Returns how many moved.
    std::size_t MoveEntitiesRelativeTo(std::span<scene::Entity *const> entities, scene::Entity &target,
                                       HierarchyDropZone zone, bool targetExpanded);
}
