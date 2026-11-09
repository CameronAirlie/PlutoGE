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
    bool ScriptSourceWatch::IsAssemblyStale(const std::filesystem::path &assembly, const std::filesystem::path &scriptCore,
                                            const Snapshot &sources)
    {
        const bool hasScripts = std::any_of(sources.begin(), sources.end(), [](const auto &source)
                                            { return source.first.ends_with(".cs"); });
        if (!hasScripts) return false;
        std::error_code error;
        const auto built = std::filesystem::last_write_time(assembly, error);
        if (error) return true;
        if (!scriptCore.empty())
        {
            const auto core = std::filesystem::last_write_time(scriptCore, error);
            if (!error && core > built) return true;
        }
        return std::any_of(sources.begin(), sources.end(), [&](const auto &source) { return source.second.first > built; });
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
