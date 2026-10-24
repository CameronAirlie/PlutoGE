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
    // (its own caches and statistics). It may create and update resources through the device from any thread.
    class ParallelRecording
    {
    public:
        using Job = std::function<void(rhi::ICommandContext &)>;

        ParallelRecording(rhi::ICommandContext &commands, core::TaskPool *pool) : m_commands(commands), m_pool(pool) {}

        void Add(Job job) { m_jobs.push_back(std::move(job)); }

        // Records every job added, then forgets them. Returns true when they recorded in parallel.
        bool Run();

        // CPU time each job of the last Run took, in the order added.
        [[nodiscard]] const std::vector<float> &JobMilliseconds() const noexcept { return m_jobMilliseconds; }

    private:
        void RunJob(std::size_t index, rhi::ICommandContext &context);

        rhi::ICommandContext &m_commands;
        core::TaskPool *m_pool;
        std::vector<Job> m_jobs;
        std::vector<float> m_jobMilliseconds;
    };
}
