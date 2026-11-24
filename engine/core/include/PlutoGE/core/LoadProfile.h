#pragma once

#include "PlutoGE/core/CpuTrace.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace PlutoGE::core
{
    // Where a host session's loading time goes, for finding load-time work and checking that it shrank.
    // - Milestones: wall time since the process started ("engine initialised", "first frame", ...), marked from any
    //   thread.
    // - Phases: the CPU scopes (CpuScope) the calling thread records inside a LoadProfile::Phase, summed by name. Code
    //   below the hosts only places CpuScopes; it never depends on this class.
    // Off unless enabled (PLUTO_LOAD_PROFILE=<report file>, or Enable); while off, Mark and Phase cost a branch.
    class LoadProfile
    {
    public:
        using Clock = std::chrono::steady_clock;

        struct Milestone
        {
            std::string name;
            double ms = 0.0; // since the process started
        };

        struct ScopeTotal
        {
            std::string name;
            std::size_t calls = 0;
            double totalMs = 0.0; // including nested scopes
            double selfMs = 0.0;  // excluding them
            double maxMs = 0.0;
        };

        static LoadProfile &Get();

        // Enables when PLUTO_LOAD_PROFILE names a report file; returns whether the profile is enabled.
        bool EnableFromEnvironment();
        void Enable(std::filesystem::path reportPath);
        [[nodiscard]] bool IsEnabled() const noexcept { return m_enabled; }

        void Mark(std::string_view milestone);
        // Wall time since the process started (since first use where the platform cannot tell).
        [[nodiscard]] double ElapsedMs() const;

        [[nodiscard]] std::vector<Milestone> Milestones() const;
        // Phase scopes summed by name, largest self time first.
        [[nodiscard]] std::vector<ScopeTotal> Scopes() const;
        [[nodiscard]] std::string Report() const;
        // Writes Report() to the enabled report file. Hosts call it once their session is loaded (and again later to
        // include later milestones: each write replaces the file).
        bool Write(std::string *errorMessage = nullptr) const;

        // Records the calling thread's CpuScopes while it lives (as one scope named after the phase). A phase opened
        // inside another on the same thread is a scope of the outer one. With markEnd, its end is also a milestone.
        // Scopes opened inside a phase must close before it does (declare it before them).
        class Phase
        {
        public:
            explicit Phase(std::string_view name, bool markEnd = false);
            ~Phase();
            Phase(const Phase &) = delete;
            Phase &operator=(const Phase &) = delete;

        private:
            std::optional<CpuTrace> m_trace;
            std::optional<CpuScope> m_scope;
            Phase *m_outer = nullptr;
            std::string m_endMilestone;
        };

    private:
        LoadProfile();
        void Merge(const std::vector<CpuSample> &samples);

        Clock::time_point m_origin;
        std::atomic<bool> m_enabled{false};
        std::filesystem::path m_reportPath;
        mutable std::mutex m_mutex;
        std::vector<Milestone> m_milestones;
        std::unordered_map<std::string, ScopeTotal> m_scopes; // by name
        std::size_t m_droppedScopes = 0;
    };
}
