#pragma once

#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace PlutoGE::ui
{
    class EditorShell;

    // Public, instance-owned extension point. Callbacks run on the editor thread.
    // Extensions retain their registration IDs and unregister before unloading.
    class AuthoringRegistry
    {
    public:
        enum class Kind { ProjectTemplate, GameplayKit, Command, Inspector };
        struct Entry
        {
            std::string id, label, description;
            Kind kind = Kind::Command;
            std::function<void(EditorShell &)> execute;
            std::function<bool(const EditorShell &)> available;
        };
        void Register(Entry entry)
        {
            if (entry.id.empty() || entry.label.empty() || !entry.execute)
                throw std::invalid_argument("Authoring entries require an ID, label and callback");
            const auto id = entry.id;
            if (!m_entries.emplace(id, std::move(entry)).second)
                throw std::invalid_argument("Duplicate authoring entry: " + id);
        }
        bool Unregister(const std::string &id) { return m_entries.erase(id) != 0; }
        std::vector<Entry> List(Kind kind) const
        {
            std::vector<Entry> entries;
            for (const auto &[id, entry] : m_entries) if (entry.kind == kind) entries.push_back(entry);
            return entries; // Snapshot: commands may register/unregister other commands safely.
        }
    private:
        std::map<std::string, Entry, std::less<>> m_entries;
    };
    void RegisterBuiltinAuthoring(AuthoringRegistry &registry);
}
