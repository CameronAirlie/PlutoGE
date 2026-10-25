#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace PlutoGE::ui
{
    // IDs survive scene snapshot restoration; the last ID is the primary entity.
    class EntitySelection
    {
    public:
        using ID = std::uint32_t;
        const std::vector<ID> &Ids() const { return m_ids; }
        // Advances on every mutation so views can react to selection changes
        // made elsewhere (for example revealing a viewport pick).
        std::uint64_t Revision() const { return m_revision; }
        bool Contains(ID id) const { return std::find(m_ids.begin(), m_ids.end(), id) != m_ids.end(); }
        void Clear() { m_ids.clear(); m_anchor = 0; ++m_revision; }
        void Set(const std::vector<ID> &ids)
        {
            Clear();
            for (auto id : ids)
                if (id && !Contains(id)) m_ids.push_back(id);
            if (!m_ids.empty()) m_anchor = m_ids.back();
        }
        void Click(ID id, bool control, bool shift, const std::vector<ID> &visible = {})
        {
            if (!id) { if (!control && !shift) Clear(); return; }
            ++m_revision;
            auto first = std::find(visible.begin(), visible.end(), m_anchor);
            auto last = std::find(visible.begin(), visible.end(), id);
            if (shift && first != visible.end() && last != visible.end())
            {
                if (!control) m_ids.clear();
                if (first > last) std::swap(first, last);
                for (; first <= last; ++first)
                    if (!Contains(*first)) m_ids.push_back(*first);
                m_ids.erase(std::remove(m_ids.begin(), m_ids.end(), id), m_ids.end());
                m_ids.push_back(id);
                return;
            }
            if (!control && !shift) m_ids.clear();
            if (control && !shift && Contains(id))
                m_ids.erase(std::remove(m_ids.begin(), m_ids.end(), id), m_ids.end());
            else if (!Contains(id)) m_ids.push_back(id);
            m_anchor = id;
        }
        template<class Exists> void Prune(Exists exists)
        {
            const auto removed = std::remove_if(m_ids.begin(), m_ids.end(), [&](ID id) { return !exists(id); });
            if (removed != m_ids.end()) ++m_revision;
            m_ids.erase(removed, m_ids.end());
            if (!exists(m_anchor)) m_anchor = m_ids.empty() ? 0 : m_ids.back();
        }
    private:
        std::vector<ID> m_ids;
        ID m_anchor = 0;
        std::uint64_t m_revision = 0;
    };
}
