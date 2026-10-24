#include "PlutoGE/core/TaskPool.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace PlutoGE::core
{
    namespace
    {
        thread_local bool t_insideTask = false;

        void NameCurrentThread([[maybe_unused]] unsigned index)
        {
#ifdef _WIN32
            const auto name = L"PlutoGE task worker " + std::to_wstring(index);
            SetThreadDescription(GetCurrentThread(), name.c_str());
#endif
        }
    }

    struct TaskPool::Impl
    {
        std::mutex mutex;
        std::condition_variable ready, finished;
        std::mutex runMutex;   // One batch at a time.
        std::vector<std::thread> workers;

        // The active batch. Written under `mutex` before workers are woken.
        const std::function<void(std::size_t)> *task = nullptr;
        std::size_t count = 0;
        std::atomic<std::size_t> next{0};
        std::uint64_t generation = 0;
        unsigned busyWorkers = 0;
        bool stopping = false;
        std::exception_ptr error;
        std::mutex errorMutex;

        // Claims tasks until the batch is exhausted.
        void Drain()
        {
            const bool outer = !t_insideTask;
            t_insideTask = true;
            for (auto index = next.fetch_add(1, std::memory_order_relaxed); index < count;
                 index = next.fetch_add(1, std::memory_order_relaxed))
            {
                try
                {
                    (*task)(index);
                }
                catch (...)
                {
                    std::lock_guard lock(errorMutex);
                    if (!error) error = std::current_exception();
                }
            }
            if (outer) t_insideTask = false;
        }

        void WorkerLoop(unsigned index)
        {
            NameCurrentThread(index);
            std::uint64_t observed = 0;
            std::unique_lock lock(mutex);
            for (;;)
            {
                ready.wait(lock, [&] { return stopping || generation != observed; });
                if (stopping) return;
                observed = generation;
                lock.unlock();
                Drain();
                lock.lock();
                if (--busyWorkers == 0) finished.notify_one();
            }
        }

        void Stop()
        {
            {
                std::lock_guard lock(mutex);
                stopping = true;
            }
            ready.notify_all();
            for (auto &worker : workers)
                if (worker.joinable()) worker.join();
        }
    };

    TaskPool::TaskPool(unsigned workerCount) : m_impl(std::make_unique<Impl>())
    {
        try
        {
            for (unsigned index = 0; index < workerCount; ++index)
                m_impl->workers.emplace_back([impl = m_impl.get(), index] { impl->WorkerLoop(index + 1); });
        }
        catch (...)
        {
            m_impl->Stop();
            throw;
        }
    }

    TaskPool::~TaskPool() { m_impl->Stop(); }

    unsigned TaskPool::DefaultWorkerCount() noexcept
    {
        const unsigned hardware = std::thread::hardware_concurrency();
        return hardware > 1 ? std::min(hardware - 1, MaxDefaultWorkers) : 0u;
    }

    unsigned TaskPool::WorkerCount() const noexcept { return static_cast<unsigned>(m_impl->workers.size()); }

    bool TaskPool::InsideTask() noexcept { return t_insideTask; }

    void TaskPool::Run(std::size_t count, const std::function<void(std::size_t)> &task)
    {
        if (count == 0) return;
        // Inline: nothing to share, no workers, or a nested batch (waiting on workers from a task could deadlock).
        if (count == 1 || m_impl->workers.empty() || t_insideTask)
        {
            const bool outer = !t_insideTask;
            t_insideTask = true;
            std::exception_ptr error;
            for (std::size_t index = 0; index < count; ++index)
            {
                try { task(index); }
                catch (...) { if (!error) error = std::current_exception(); }
            }
            if (outer) t_insideTask = false;
            if (error) std::rethrow_exception(error);
            return;
        }

        std::lock_guard run(m_impl->runMutex);
        auto &impl = *m_impl;
        {
            std::lock_guard lock(impl.mutex);
            impl.task = &task;
            impl.count = count;
            impl.next.store(0, std::memory_order_relaxed);
            impl.error = nullptr;
            // Every worker wakes; those finding no task left return at once.
            impl.busyWorkers = static_cast<unsigned>(impl.workers.size());
            ++impl.generation;
        }
        impl.ready.notify_all();
        impl.Drain();
        {
            std::unique_lock lock(impl.mutex);
            impl.finished.wait(lock, [&] { return impl.busyWorkers == 0; });
            impl.task = nullptr;
        }
        if (impl.error) std::rethrow_exception(impl.error);
    }

    TaskPool &TaskPool::Shared()
    {
        static TaskPool pool;
        return pool;
    }
}
