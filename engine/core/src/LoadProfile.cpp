#include "PlutoGE/core/LoadProfile.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <unordered_map>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace PlutoGE::core
{
    namespace
    {
        thread_local LoadProfile::Phase *t_phase = nullptr;
        constexpr std::size_t kPhaseSampleCapacity = std::size_t(1) << 20;

        // How long ago the process started; zero where the platform cannot tell.
        LoadProfile::Clock::duration ProcessAge()
        {
#ifdef _WIN32
            FILETIME creation{}, exit{}, kernel{}, user{}, now{};
            if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) return {};
            GetSystemTimePreciseAsFileTime(&now);
            const auto ticks = [](const FILETIME &time) {
                return (static_cast<unsigned long long>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
            };
            const auto created = ticks(creation), current = ticks(now);
            if (current <= created) return {};
            // FILETIME ticks are 100 ns.
            return std::chrono::duration_cast<LoadProfile::Clock::duration>(std::chrono::nanoseconds((current - created) * 100));
#else
            return {};
#endif
        }
    }

    LoadProfile::LoadProfile() : m_origin(Clock::now() - ProcessAge()) {}

    LoadProfile &LoadProfile::Get()
    {
        static LoadProfile profile;
        return profile;
    }

    bool LoadProfile::EnableFromEnvironment()
    {
        if (const char *path = std::getenv("PLUTO_LOAD_PROFILE"); path && *path)
            Enable(path);
        return m_enabled;
    }

    void LoadProfile::Enable(std::filesystem::path reportPath)
    {
        std::lock_guard lock(m_mutex);
        m_reportPath = std::move(reportPath);
        m_enabled = true;
    }

    double LoadProfile::ElapsedMs() const
    {
        return std::chrono::duration<double, std::milli>(Clock::now() - m_origin).count();
    }

    void LoadProfile::Mark(std::string_view milestone)
    {
        if (!m_enabled) return;
        const double ms = ElapsedMs();
        std::lock_guard lock(m_mutex);
        m_milestones.push_back({std::string(milestone), ms});
    }

    std::vector<LoadProfile::Milestone> LoadProfile::Milestones() const
    {
        std::lock_guard lock(m_mutex);
        return m_milestones;
    }

    std::vector<LoadProfile::ScopeTotal> LoadProfile::Scopes() const
    {
        std::vector<ScopeTotal> scopes;
        {
            std::lock_guard lock(m_mutex);
            scopes.reserve(m_scopes.size());
            for (const auto &[name, total] : m_scopes)
            {
                scopes.push_back(total);
                scopes.back().name = name;
            }
        }
        std::stable_sort(scopes.begin(), scopes.end(), [](const auto &a, const auto &b) { return a.selfMs > b.selfMs; });
        return scopes;
    }

    void LoadProfile::Merge(const std::vector<CpuSample> &samples)
    {
        const auto self = CpuSelfTimes(samples);
        std::lock_guard lock(m_mutex);
        for (std::size_t i = 0; i < samples.size(); ++i)
        {
            const auto &sample = samples[i];
            // A scope nested in one of the same name (recursion) is already inside the outer total.
            bool recursive = false;
            for (int parent = sample.parent; parent >= 0 && !recursive; parent = samples[static_cast<std::size_t>(parent)].parent)
                recursive = samples[static_cast<std::size_t>(parent)].name == sample.name;
            auto &total = m_scopes[sample.name];
            ++total.calls;
            if (!recursive) total.totalMs += sample.durationMs;
            total.selfMs += self[i];
            total.maxMs = std::max(total.maxMs, static_cast<double>(sample.durationMs));
        }
    }

    std::string LoadProfile::Report() const
    {
        std::string text = "PlutoGE load profile\n\nMilestones (ms since the process started)\n";
        char line[512];
        for (const auto &milestone : Milestones())
        {
            std::snprintf(line, sizeof(line), "%10.1f  %s\n", milestone.ms, milestone.name.c_str());
            text += line;
        }
        text += "\nMain-thread scopes inside load phases, by self time\n";
        std::snprintf(line, sizeof(line), "%10s %10s %8s %9s  %s\n", "self ms", "total ms", "calls", "max ms", "scope");
        text += line;
        for (const auto &scope : Scopes())
        {
            std::snprintf(line, sizeof(line), "%10.1f %10.1f %8zu %9.1f  %s\n", scope.selfMs, scope.totalMs, scope.calls,
                          scope.maxMs, scope.name.c_str());
            text += line;
        }
        std::lock_guard lock(m_mutex);
        if (m_droppedScopes)
            text += "\n" + std::to_string(m_droppedScopes) + " scopes were not recorded (capacity).\n";
        return text;
    }

    bool LoadProfile::Write(std::string *errorMessage) const
    {
        if (!m_enabled) return true;
        std::ofstream output(m_reportPath, std::ios::binary | std::ios::trunc);
        output << Report();
        if (output) return true;
        if (errorMessage) *errorMessage = "Could not write the load profile to " + m_reportPath.string();
        return false;
    }

    LoadProfile::Phase::Phase(std::string_view name, bool markEnd)
    {
        auto &profile = LoadProfile::Get();
        if (!profile.IsEnabled()) return;
        if (markEnd) m_endMilestone = name;
        // A phase inside another records into the outer phase's trace.
        if (t_phase && CpuTrace::current)
        {
            m_scope.emplace(name);
            return;
        }
        m_outer = t_phase;
        t_phase = this;
        m_trace.emplace(true, CpuTrace::Clock::now(), kPhaseSampleCapacity);
        m_scope.emplace(name);
    }

    LoadProfile::Phase::~Phase()
    {
        m_scope.reset();
        auto &profile = LoadProfile::Get();
        if (!m_endMilestone.empty()) profile.Mark(m_endMilestone);
        if (!m_trace) return;
        const auto dropped = m_trace->GetDroppedCount();
        const auto samples = m_trace->TakeSamples();
        m_trace.reset();
        t_phase = m_outer;
        try
        {
            profile.Merge(samples);
            std::lock_guard lock(profile.m_mutex);
            profile.m_droppedScopes += dropped;
        }
        catch (...) {} // Diagnostics never change the outcome of the work they measure.
    }
}
