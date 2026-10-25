#pragma once

#include <algorithm>
#include <span>
#include <string>
#include <vector>

namespace PlutoGE::ui
{
    // Ordered multi-selection of content items keyed by absolute path string.
    // The most recently clicked item is primary; Shift ranges extend from the
    // anchor across the items currently shown.
    class ContentSelection
    {
    public:
        using Key = std::string;

        const std::vector<Key> &Keys() const { return m_keys; }
        bool Empty() const { return m_keys.empty(); }
        std::size_t Size() const { return m_keys.size(); }
        bool Contains(const Key &key) const { return std::find(m_keys.begin(), m_keys.end(), key) != m_keys.end(); }
        const Key *Primary() const { return m_keys.empty() ? nullptr : &m_keys.back(); }
        const Key &Anchor() const { return m_anchor; }

        void Clear()
        {
            m_keys.clear();
            m_anchor.clear();
        }

        void Set(std::span<const Key> keys)
        {
            Clear();
            for (const auto &key : keys)
                if (!key.empty() && !Contains(key)) m_keys.push_back(key);
            if (!m_keys.empty()) m_anchor = m_keys.back();
        }

        void Select(const Key &key) { Set(std::span<const Key>(&key, 1)); }

        // Plain click selects only `key`; Ctrl toggles it; Shift selects the
        // shown range from the anchor (added to the selection with Ctrl).
        void Click(const Key &key, bool control, bool shift, std::span<const Key> shown)
        {
            const auto first = std::find(shown.begin(), shown.end(), m_anchor);
            const auto last = std::find(shown.begin(), shown.end(), key);
            if (shift && first != shown.end() && last != shown.end())
            {
                if (!control) m_keys.clear();
                for (auto it = std::min(first, last); it <= std::max(first, last); ++it)
                    if (!Contains(*it)) m_keys.push_back(*it);
                MakePrimary(key);
                return;
            }
            if (control && Contains(key))
            {
                m_keys.erase(std::remove(m_keys.begin(), m_keys.end(), key), m_keys.end());
                m_anchor = key;
                return;
            }
            if (!control) m_keys.clear();
            MakePrimary(key);
            m_anchor = key;
        }

        // Drops keys for items that no longer exist (after deletes or refreshes).
        template <class Exists>
        void Prune(Exists exists)
        {
            m_keys.erase(std::remove_if(m_keys.begin(), m_keys.end(), [&](const Key &key) { return !exists(key); }), m_keys.end());
            if (!m_anchor.empty() && !exists(m_anchor)) m_anchor = m_keys.empty() ? Key{} : m_keys.back();
        }

        // Rewrites keys after a rename or move; `remap` returns the new key.
        template <class Mapping>
        void Remap(Mapping remap)
        {
            for (auto &key : m_keys) key = remap(key);
            if (!m_anchor.empty()) m_anchor = remap(m_anchor);
        }

    private:
        void MakePrimary(const Key &key)
        {
            m_keys.erase(std::remove(m_keys.begin(), m_keys.end(), key), m_keys.end());
            m_keys.push_back(key);
        }

        std::vector<Key> m_keys;
        Key m_anchor;
    };
}
