#include "PlutoGE/ui/ScriptSourceWatch.h"
#include <algorithm>
#include <cctype>

namespace PlutoGE::ui
{
    ScriptSourceWatch::Snapshot ScriptSourceWatch::Capture(const std::filesystem::path &root)
    {
        Snapshot result;
        if (!std::filesystem::exists(root)) return result;
        for (auto it = std::filesystem::recursive_directory_iterator(root); it != std::filesystem::recursive_directory_iterator(); ++it)
        {
            if (it->is_symlink()) { it.disable_recursion_pending(); continue; }
            if (it->is_directory())
            {
                const auto name = it->path().filename().string();
                if (name == "bin" || name == "obj" || name == ".git" || name == "Managed") it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file()) continue;
            auto extension = it->path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (extension == ".cs" || extension == ".csproj" || extension == ".props" || extension == ".targets")
                result.emplace(it->path().generic_string(), Stamp{it->last_write_time(), it->file_size()});
        }
        return result;
    }
    bool ScriptSourceWatch::Observe(Snapshot snapshot, Clock::time_point now)
    {
        const bool changed = m_primed && snapshot != m_snapshot;
        m_primed = true;
        m_snapshot = std::move(snapshot);
        if (changed) { m_pending = true; m_changed = now; }
        return changed;
    }
}
