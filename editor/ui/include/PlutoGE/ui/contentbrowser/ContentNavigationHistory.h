#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace PlutoGE::ui
{
    // Browser-style folder history. Visiting a new folder discards any forward
    // entries; consecutive visits to the same folder are recorded once.
    class ContentNavigationHistory
    {
    public:
        static constexpr std::size_t kMaxEntries = 64;

        ContentNavigationHistory() { m_entries.emplace_back(); }

        const std::string &Current() const { return m_entries[m_index]; }
        bool CanGoBack() const { return m_index > 0; }
        bool CanGoForward() const { return m_index + 1 < m_entries.size(); }

        void Visit(const std::string &folder)
        {
            if (folder == Current()) return;
            m_entries.resize(m_index + 1);
            m_entries.push_back(folder);
            if (m_entries.size() > kMaxEntries) m_entries.erase(m_entries.begin());
            m_index = m_entries.size() - 1;
        }

        const std::string &Back()
        {
            if (CanGoBack()) --m_index;
            return Current();
        }

        const std::string &Forward()
        {
            if (CanGoForward()) ++m_index;
            return Current();
        }

        // Applies a rename or deletion to every recorded folder, then collapses
        // neighbouring duplicates the rewrite may have produced.
        template <class Mapping>
        void Remap(Mapping remap)
        {
            for (auto &entry : m_entries) entry = remap(entry);
            std::vector<std::string> collapsed;
            std::size_t index = 0;
            for (std::size_t i = 0; i < m_entries.size(); ++i)
            {
                // A duplicate merges into the slot before it.
                if (collapsed.empty() || collapsed.back() != m_entries[i]) collapsed.push_back(m_entries[i]);
                if (i == m_index) index = collapsed.size() - 1;
            }
            m_entries = std::move(collapsed);
            m_index = index;
        }

        void Reset()
        {
            m_entries.assign(1, {});
            m_index = 0;
        }

    private:
        std::vector<std::string> m_entries;
        std::size_t m_index = 0;
    };
}
