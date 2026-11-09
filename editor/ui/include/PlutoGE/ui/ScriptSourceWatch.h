#pragma once
#include <chrono>
#include <filesystem>
#include <map>
#include <string>

namespace PlutoGE::ui
{
    // Debounce source inputs only, excluding build outputs. One failure is retried
    // only after another edit, so a compiler error cannot create a rebuild loop.
    class ScriptSourceWatch
    {
    public:
        using Clock = std::chrono::steady_clock;
        using Stamp = std::pair<std::filesystem::file_time_type, std::uintmax_t>;
        using Snapshot = std::map<std::string, Stamp>;
        static Snapshot Capture(const std::filesystem::path &root);
        // True when the built assembly is missing or older than the ScriptCore it loads against or any source,
        // e.g. after an engine update or opening the project on another machine. Edits made while the editor
        // runs are caught by Observe; this covers what changed while it was closed.
        static bool IsAssemblyStale(const std::filesystem::path &assembly, const std::filesystem::path &scriptCore,
                                    const Snapshot &sources);
        bool Observe(Snapshot snapshot, Clock::time_point now);
        bool IsPrimed() const { return m_primed; }
        void Request(Clock::time_point now) { m_pending = true; m_changed = now; }
        bool Ready(Clock::time_point now) const { return m_pending && now - m_changed >= std::chrono::milliseconds(750); }
        void Acknowledge() { m_pending = false; }
        void Reset() { *this = {}; }
    private:
        Snapshot m_snapshot;
        Clock::time_point m_changed{};
        bool m_primed = false, m_pending = false;
    };
}
