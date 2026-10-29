#pragma once

#include "PlutoGE/render/rhi/RenderDevice.h"

#include <chrono>
#include <cstddef>
#include <functional>
#include <vector>

namespace PlutoGE::core
{
    class TaskPool;
}

namespace PlutoGE::render
{
    // Records passes that are independent on the CPU but ordered on the GPU. When the device can record in parallel
    // and a task pool is given, the jobs run concurrently, each into its own command context; otherwise they run one
    // after another on the frame's context. Either way their commands execute in the order the jobs were added.
    //
    // A job may read anything prepared before Run, and may write only state no other job of the same run touches
    // (its own caches and statistics), unless the jobs share a chain. It may create and update resources through the
    // device from any thread.
    class ParallelRecording
    {
    public:
        using Job = std::function<void(rhi::ICommandContext &)>;

        ParallelRecording(rhi::ICommandContext &commands, core::TaskPool *pool) : m_commands(commands), m_pool(pool) {}

        // Returns the job's index into JobMilliseconds.
        std::size_t Add(Job job)
        {
            m_jobs.push_back(std::move(job));
            m_chains.push_back(m_jobs.size() - 1);
            return m_jobs.size() - 1;
        }

        // Adds `job` to the chain of the earlier job `previous`. A chain records on one thread in the order added,
        // each job into its own context, so a later job may continue CPU state an earlier one left (binding caches,
        // single-threaded parameter caches) while other jobs record in parallel between them in GPU order.
        std::size_t AddAfter(std::size_t previous, Job job)
        {
            const auto index = Add(std::move(job));
            m_chains[index] = m_chains[previous];
            return index;
        }

        // Takes the next position in GPU order for a job given later with Set: work whose state is only prepared
        // further on can still execute before jobs added in between. An unset reservation records nothing.
        std::size_t Reserve() { return Add({}); }
        void Set(std::size_t index, Job job) { m_jobs[index] = std::move(job); }

        // Records every job added, then forgets them. Returns true when they recorded in parallel.
        bool Run();

        // CPU time each job of the last Run took, in the order added.
        [[nodiscard]] const std::vector<float> &JobMilliseconds() const noexcept { return m_jobMilliseconds; }

    private:
        void RunJob(std::size_t index, rhi::ICommandContext &context);
        void Clear() noexcept
        {
            m_jobs.clear();
            m_chains.clear();
        }

        rhi::ICommandContext &m_commands;
        core::TaskPool *m_pool;
        std::vector<Job> m_jobs;
        // The first job of each job's chain.
        std::vector<std::size_t> m_chains;
        std::vector<float> m_jobMilliseconds;
    };
}
