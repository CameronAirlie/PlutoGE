#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace PlutoGE::core
{
    enum class CpuCategory : std::uint8_t { Scripts, Rendering, Physics, Animation, Audio, UI, Wait, Other, Count };

    struct CpuSample
    {
        std::string name;
        std::string context;
        float startMs = 0.0f;
        float durationMs = 0.0f;
        int parent = -1;
        int depth = 0;
        CpuCategory category = CpuCategory::Other;
    };

    // One recorder per thread. The editor currently attaches only the main thread.
    // No locks, clock reads or string copies are needed for disabled scopes.
    class CpuTrace
    {
    public:
        using Clock = std::chrono::steady_clock;
        static constexpr std::size_t MaxSamples = 4096;
        static constexpr std::size_t MaxLabelLength = 160;
        inline static thread_local CpuTrace *current = nullptr;

        explicit CpuTrace(bool enabled, Clock::time_point origin = Clock::now())
            : m_origin(origin), m_previous(current), m_enabled(enabled)
        {
            if (enabled) m_samples.reserve(256);
            current = enabled ? this : nullptr;
        }
        ~CpuTrace() { current = m_previous; }
        CpuTrace(const CpuTrace &) = delete;
        CpuTrace &operator=(const CpuTrace &) = delete;

        int Begin(std::string_view name, CpuCategory category, std::string_view context = {},
                  Clock::time_point now = Clock::now())
        {
            if (!m_enabled) return -1;
            if (m_samples.size() >= MaxSamples) { ++m_dropped; return -1; }
            const int index = static_cast<int>(m_samples.size());
            const int depth = m_parent < 0 ? 0 : m_samples[m_parent].depth + 1;
            m_samples.push_back({std::string(name.substr(0, MaxLabelLength)),
                                 std::string(context.substr(0, MaxLabelLength)),
                                 std::chrono::duration<float, std::milli>(now - m_origin).count(),
                                 0.0f, m_parent, depth, category});
            m_parent = index;
            return index;
        }
        // Scopes must close in LIFO order, before TakeSamples or recorder destruction.
        void End(int index, Clock::time_point now = Clock::now()) noexcept
        {
            if (index < 0) return;
            auto &sample = m_samples[static_cast<std::size_t>(index)];
            sample.durationMs = std::max(0.0f, std::chrono::duration<float, std::milli>(now - m_origin).count() - sample.startMs);
            m_parent = sample.parent;
        }
        [[nodiscard]] std::uint32_t GetDroppedCount() const noexcept { return m_dropped; }
        [[nodiscard]] std::vector<CpuSample> TakeSamples() { m_enabled = false; return std::move(m_samples); }

    private:
        Clock::time_point m_origin;
        CpuTrace *m_previous;
        bool m_enabled;
        std::vector<CpuSample> m_samples;
        int m_parent = -1;
        std::uint32_t m_dropped = 0;
    };

    class CpuScope
    {
    public:
        CpuScope(std::string_view name, CpuCategory category = CpuCategory::Other, std::string_view context = {})
            : m_trace(CpuTrace::current), m_index(m_trace ? m_trace->Begin(name, category, context) : -1) {}
        ~CpuScope() { End(); }
        CpuScope(const CpuScope &) = delete;
        CpuScope &operator=(const CpuScope &) = delete;
        void End() noexcept
        {
            if (m_trace && m_index >= 0) m_trace->End(m_index);
            m_trace = nullptr;
        }
    private:
        CpuTrace *m_trace;
        int m_index;
    };

    // Subtract immediate children only: descendants are already included in them.
    inline std::vector<float> CpuSelfTimes(const std::vector<CpuSample> &samples)
    {
        std::vector<float> result;
        result.reserve(samples.size());
        for (const auto &sample : samples) result.push_back(sample.durationMs);
        for (std::size_t i = 0; i < samples.size(); ++i)
            if (samples[i].parent >= 0 && static_cast<std::size_t>(samples[i].parent) < i)
                result[static_cast<std::size_t>(samples[i].parent)] -= samples[i].durationMs;
        for (auto &value : result) value = std::max(0.0f, value);
        return result;
    }
}
