#pragma once

#include "PlutoGE/scene/Entity.h"

#include <cstdint>
#include <unordered_set>

namespace PlutoGE::ui
{
    // Editor-session lock state. A locked entity and its descendants cannot be
    // picked in viewports but stay selectable from the hierarchy. Locks are
    // keyed by entity ID, so they survive undo snapshot restoration; they are
    // not saved with the scene and reset when another scene is opened.
    class EntityLocks
    {
    public:
        using ID = std::uint32_t;

        bool IsLocked(ID id) const { return m_locked.contains(id); }
        void SetLocked(ID id, bool locked)
        {
            if (locked) m_locked.insert(id);
            else m_locked.erase(id);
        }
        void Clear() { m_locked.clear(); }

        // True when the entity or any ancestor is locked.
        bool IsLockedInHierarchy(const scene::Entity *entity) const
        {
            if (m_locked.empty()) return false;
            for (; entity; entity = entity->GetParent())
                if (IsLocked(entity->GetID())) return true;
            return false;
        }

    private:
        std::unordered_set<ID> m_locked;
    };
}
