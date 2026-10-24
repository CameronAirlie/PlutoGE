#pragma once

#include <cstddef>
#include <functional>
#include <memory>

namespace PlutoGE::core
{
    // A fixed set of worker threads that run batches of independent tasks. The calling thread takes part in every
    // batch, so a pool of N workers runs up to N + 1 tasks at once, and Run returns only once each task has finished.
    // Tasks of one batch must not wait on each other. A Run issued from inside a task (a nested batch) executes
    // inline on that thread instead of deadlocking. If tasks throw, the batch still completes and the first exception
    // is rethrown from Run. Batches from different threads are serialized.
    class TaskPool
    {
    public:
        // workerCount 0 makes every batch run on the caller (useful for tests and single-core machines).
        explicit TaskPool(unsigned workerCount = DefaultWorkerCount());
        ~TaskPool();
        TaskPool(const TaskPool &) = delete;
        TaskPool &operator=(const TaskPool &) = delete;

        // Hardware threads minus the caller, at most MaxDefaultWorkers.
        [[nodiscard]] static unsigned DefaultWorkerCount() noexcept;
        static constexpr unsigned MaxDefaultWorkers = 7;

        [[nodiscard]] unsigned WorkerCount() const noexcept;
        // Tasks that can run at once: the workers plus the caller.
        [[nodiscard]] unsigned Concurrency() const noexcept { return WorkerCount() + 1; }

        // Runs task(0) .. task(count - 1), each exactly once, in any order and on any participating thread.
        void Run(std::size_t count, const std::function<void(std::size_t)> &task);

        // True on a thread currently executing a task of any pool.
        [[nodiscard]] static bool InsideTask() noexcept;

        // The process-wide pool engine systems share (created on first use).
        [[nodiscard]] static TaskPool &Shared();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
