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
        // One Run's tasks. Lives on its caller's stack until every task has finished and no worker still refers to it.
        struct Batch
        {
            const std::function<void(std::size_t)> *task = nullptr;
            std::size_t count = 0;
            std::atomic<std::size_t> next{0};       // Next task to claim.
            std::atomic<std::size_t> completed{0};
            unsigned workers = 0;                   // Workers inside Drain; guarded by Impl::mutex.
            std::exception_ptr error;
            std::mutex errorMutex;

            [[nodiscard]] bool HasUnclaimedTasks() const noexcept { return next.load(std::memory_order_relaxed) < count; }
        };

        std::mutex mutex;
        std::condition_variable ready, finished;
        std::vector<std::thread> workers;
        // Batches with tasks left to run, oldest first. Batches from several threads run at once, sharing the workers.
        std::vector<Batch *> batches;
        bool stopping = false;

        // Claims and runs the batch's tasks until none are left to claim.
        void Drain(Batch &batch)
        {
            const bool outer = !t_insideTask;
            t_insideTask = true;
            for (auto index = batch.next.fetch_add(1, std::memory_order_relaxed); index < batch.count;
                 index = batch.next.fetch_add(1, std::memory_order_relaxed))
            {
                try
                {
                    (*batch.task)(index);
                }
                catch (...)
                {
                    std::lock_guard lock(batch.errorMutex);
                    if (!batch.error) batch.error = std::current_exception();
                }
                if (batch.completed.fetch_add(1, std::memory_order_acq_rel) + 1 == batch.count)
                {
                    std::lock_guard lock(mutex);
                    finished.notify_all();
                }
            }
            if (outer) t_insideTask = false;
        }

        Batch *FindWork()
        {
            for (auto *batch : batches)
                if (batch->HasUnclaimedTasks()) return batch;
            return nullptr;
        }

        void WorkerLoop(unsigned index)
        {
            NameCurrentThread(index);
            std::unique_lock lock(mutex);
            for (;;)
            {
                Batch *batch = nullptr;
                ready.wait(lock, [&] { return stopping || (batch = FindWork()) != nullptr; });
                if (stopping) return;
                ++batch->workers;
                lock.unlock();
                Drain(*batch);
                lock.lock();
                if (--batch->workers == 0) finished.notify_all();
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

        auto &impl = *m_impl;
        Impl::Batch batch;
        batch.task = &task;
        batch.count = count;
        {
            std::lock_guard lock(impl.mutex);
            impl.batches.push_back(&batch);
        }
        impl.ready.notify_all();
        impl.Drain(batch);
        {
            // Wait for tasks workers claimed, then for every worker to leave the batch before it goes out of scope.
            std::unique_lock lock(impl.mutex);
            impl.finished.wait(lock, [&]
            {
                return batch.completed.load(std::memory_order_acquire) == batch.count && batch.workers == 0;
            });
            std::erase(impl.batches, &batch);
        }
        if (batch.error) std::rethrow_exception(batch.error);
    }

    std::size_t TaskPool::RangeCount(std::size_t count, std::size_t grain) const noexcept
    {
        if (count == 0) return 0;
        const auto worthwhile = (count + std::max<std::size_t>(grain, 1) - 1) / std::max<std::size_t>(grain, 1);
        return std::clamp<std::size_t>(worthwhile, 1, Concurrency());
    }

    void TaskPool::RunRanges(std::size_t count, std::size_t grain,
                             const std::function<void(std::size_t, std::size_t, std::size_t)> &task)
    {
        const auto ranges = RangeCount(count, grain);
        Run(ranges, [&](std::size_t range)
        {
            // The first `count % ranges` ranges take one extra item.
            const auto size = count / ranges, extra = count % ranges;
            const auto begin = range * size + std::min(range, extra);
            task(range, begin, begin + size + (range < extra ? 1 : 0));
        });
    }

    TaskPool &TaskPool::Shared()
    {
        static TaskPool pool;
        return pool;
    }
}
