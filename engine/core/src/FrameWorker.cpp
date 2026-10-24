#include "PlutoGE/core/FrameWorker.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace PlutoGE::core
{
    struct FrameWorker::Impl
    {
        std::mutex mutex;
        std::condition_variable wake, idle;
        std::function<void()> job;
        std::exception_ptr error;
        bool busy = false;
        bool stopping = false;
        float lastWaitMs = 0.0f;
        std::thread thread;

        void Loop()
        {
            std::unique_lock lock(mutex);
            for (;;)
            {
                wake.wait(lock, [&] { return stopping || job; });
                if (!job) return;   // Stopping with nothing queued.
                auto current = std::move(job);
                job = nullptr;
                lock.unlock();
                std::exception_ptr failure;
                try { current(); }
                catch (...) { failure = std::current_exception(); }
                lock.lock();
                if (failure && !error) error = failure;
                busy = false;
                idle.notify_all();
            }
        }

        // Waits for the job in flight under `lock`; returns its exception, if any.
        std::exception_ptr WaitLocked(std::unique_lock<std::mutex> &lock)
        {
            const auto start = std::chrono::steady_clock::now();
            idle.wait(lock, [&] { return !busy; });
            lastWaitMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
            return std::exchange(error, nullptr);
        }
    };

    FrameWorker::FrameWorker(std::string threadName) : m_impl(std::make_unique<Impl>())
    {
        m_impl->thread = std::thread([impl = m_impl.get(), name = std::move(threadName)]
        {
#ifdef _WIN32
            SetThreadDescription(GetCurrentThread(), std::wstring(name.begin(), name.end()).c_str());
#else
            (void)name;
#endif
            impl->Loop();
        });
    }

    FrameWorker::~FrameWorker()
    {
        {
            std::unique_lock lock(m_impl->mutex);
            (void)m_impl->WaitLocked(lock);
            m_impl->stopping = true;
        }
        m_impl->wake.notify_one();
        m_impl->thread.join();
    }

    void FrameWorker::Submit(std::function<void()> job)
    {
        std::unique_lock lock(m_impl->mutex);
        if (const auto error = m_impl->WaitLocked(lock))
            std::rethrow_exception(error);
        m_impl->job = std::move(job);
        m_impl->busy = true;
        lock.unlock();
        m_impl->wake.notify_one();
    }

    void FrameWorker::Wait()
    {
        std::unique_lock lock(m_impl->mutex);
        if (const auto error = m_impl->WaitLocked(lock))
            std::rethrow_exception(error);
    }

    bool FrameWorker::IsBusy() const noexcept
    {
        std::lock_guard lock(m_impl->mutex);
        return m_impl->busy;
    }

    float FrameWorker::LastWaitMilliseconds() const noexcept
    {
        std::lock_guard lock(m_impl->mutex);
        return m_impl->lastWaitMs;
    }
}
