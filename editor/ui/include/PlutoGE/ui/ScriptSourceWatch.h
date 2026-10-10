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
        bool Observe(Snapshot snapshot, Clock::time_point now);
        bool Ready(Clock::time_point now) const { return m_pending && now - m_changed >= std::chrono::milliseconds(750); }
        void Acknowledge() { m_pending = false; }
        void Reset() { *this = {}; }
    private:
        Snapshot m_snapshot;
        Clock::time_point m_changed{};
        bool m_primed = false, m_pending = false;
    };
}
