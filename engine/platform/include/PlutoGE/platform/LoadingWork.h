#pragma once

#include <chrono>
#include <functional>
#include <future>
#include <utility>

namespace PlutoGE::platform
{
    // A scoped, owner-thread-only loading pump. Checkpoints must be outside GPU
    // command recording and container iteration that invokes gameplay callbacks.
    // Workers never inherit this context and cannot render or mutate a scene.
    class LoadingWork
    {
    public:
        explicit LoadingWork(std::function<void()> present,
                             std::chrono::milliseconds interval = std::chrono::milliseconds(8))
            : m_previous(s_current), m_present(std::move(present)), m_interval(interval)
        { s_current = this; }
        ~LoadingWork() { s_current = m_previous; }
        LoadingWork(const LoadingWork &) = delete;
        LoadingWork &operator=(const LoadingWork &) = delete;

        static bool IsActive() noexcept { return s_current != nullptr; }
        static void Checkpoint(bool force = false)
        {
            auto *work = s_current;
            if (!work || work->m_presenting) return;
            const auto now = std::chrono::steady_clock::now();
            if (!force && now - work->m_last < work->m_interval) return;
            work->m_last = now;
            work->m_presenting = true;
            try { work->m_present(); }
            catch (...) { work->m_presenting = false; throw; }
            // A slow present must not consume the next work slice. Otherwise
            // every record can present again and starve the actual loader.
            work->m_last = std::chrono::steady_clock::now();
            work->m_presenting = false;
        }

        // Only pass CPU-only, independently owned work here. Destruction of the
        // future joins the worker before its captured resources can be released.
        template<class Function> static auto Prepare(Function function)
        {
            if (!IsActive()) return function();
            auto result = std::async(std::launch::async, std::move(function));
            while (result.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready)
                Checkpoint();
            return result.get();
        }
    private:
        inline static thread_local LoadingWork *s_current = nullptr;
        LoadingWork *m_previous;
        std::function<void()> m_present;
        std::chrono::milliseconds m_interval;
        std::chrono::steady_clock::time_point m_last{};
        bool m_presenting = false;
    };
}
