#include "PlutoGE/core/TaskPool.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using PlutoGE::core::TaskPool;

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    // Every index runs exactly once, whatever the worker count.
    void RunsEachTaskOnce(unsigned workers)
    {
        TaskPool pool(workers);
        for (std::size_t count : {std::size_t{0}, std::size_t{1}, std::size_t{3}, std::size_t{100}})
        {
            std::vector<std::atomic<int>> hits(count);
            pool.Run(count, [&](std::size_t index) { hits[index].fetch_add(1); });
            for (std::size_t index = 0; index < count; ++index)
                Require(hits[index].load() == 1, "task " + std::to_string(index) + " of " + std::to_string(count) +
                                                       " ran " + std::to_string(hits[index].load()) + " times");
        }
    }

    // Tasks of one batch overlap: a two-task barrier only completes if both run at once.
    void TasksRunConcurrently()
    {
        TaskPool pool(1);
        std::atomic<int> arrived{0};
        pool.Run(2, [&](std::size_t)
        {
            arrived.fetch_add(1);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (arrived.load() < 2)
            {
                if (std::chrono::steady_clock::now() > deadline) throw std::runtime_error("tasks did not overlap");
                std::this_thread::yield();
            }
        });
    }

    void ExceptionsReachTheCaller()
    {
        TaskPool pool(2);
        std::atomic<int> ran{0};
        bool caught = false;
        try
        {
            pool.Run(8, [&](std::size_t index)
            {
                ran.fetch_add(1);
                if (index == 3) throw std::runtime_error("task failed");
            });
        }
        catch (const std::runtime_error &error)
        {
            caught = std::string(error.what()) == "task failed";
        }
        Require(caught, "a task's exception did not reach Run's caller");
        Require(ran.load() == 8, "a failing task stopped the rest of its batch");
        // The pool stays usable.
        std::atomic<int> after{0};
        pool.Run(4, [&](std::size_t) { after.fetch_add(1); });
        Require(after.load() == 4, "the pool did not recover after an exception");
    }

    void NestedRunsExecuteInline()
    {
        TaskPool pool(2);
        std::atomic<int> inner{0};
        pool.Run(3, [&](std::size_t)
        {
            Require(TaskPool::InsideTask(), "InsideTask is false inside a task");
            pool.Run(4, [&](std::size_t) { inner.fetch_add(1); });
        });
        Require(inner.load() == 12, "nested batches did not all run");
        Require(!TaskPool::InsideTask(), "InsideTask stayed true after the batch");
    }
}

int main()
{
    try
    {
        for (unsigned workers : {0u, 1u, 3u})
            RunsEachTaskOnce(workers);
        TasksRunConcurrently();
        ExceptionsReachTheCaller();
        NestedRunsExecuteInline();
        Require(TaskPool::Shared().WorkerCount() == TaskPool::DefaultWorkerCount(), "shared pool size");
        std::cout << "Task pool tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
