#pragma once

#include <cstdint>
#include <unordered_set>

namespace PlutoGE::scene
{
    class Entity;
}

namespace PlutoGE::ui
{
    // Which hierarchy rows are expanded, keyed by entity ID. The panel owns
    // this state and drives ImGui from it every frame, so expanding a subtree,
    // revealing a selection or keyboard navigation never depend on ImGui's
    // per-window ID-stack storage.
    class HierarchyExpansion
    {
    public:
        using ID = std::uint32_t;

        bool IsExpanded(ID id) const { return m_expanded.contains(id); }
        void SetExpanded(ID id, bool expanded);
        // Applies to the entity and every descendant that has children.
        void SetSubtreeExpanded(const scene::Entity &entity, bool expanded);
        // Expands every ancestor so the entity's row becomes visible.
        void ExpandAncestors(const scene::Entity &entity);
        void Clear() { m_expanded.clear(); }

    private:
        std::unordered_set<ID> m_expanded;
    };
}
