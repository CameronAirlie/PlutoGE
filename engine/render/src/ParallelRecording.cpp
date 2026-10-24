#include "ParallelRecording.h"

#include "PlutoGE/core/TaskPool.h"
#include "PlutoGE/render/ShaderGraph.h"

namespace PlutoGE::render
{
    void ParallelRecording::RunJob(std::size_t index, rhi::ICommandContext &context)
    {
        const auto start = std::chrono::steady_clock::now();
        m_jobs[index](context);
        m_jobMilliseconds[index] = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
    }

    bool ParallelRecording::Run()
    {
        const auto count = m_jobs.size();
        m_jobMilliseconds.assign(count, 0.0f);
        const bool parallel = m_pool && count > 1 && count <= m_commands.MaxParallelRecordingContexts() &&
                              m_pool->Concurrency() > 1 && !core::TaskPool::InsideTask();
        if (!parallel)
        {
            for (std::size_t index = 0; index < count; ++index)
                RunJob(index, m_commands);
            m_jobs.clear();
            return false;
        }

        const auto contexts = m_commands.BeginParallelRecording(static_cast<std::uint32_t>(count));
        // Workers read the frame's shader-graph clock, which a render thread pins for itself.
        const auto clock = ScopedShaderGraphClock::Pinned();
        try
        {
            m_pool->Run(count, [&](std::size_t index)
            {
                auto &context = *contexts[index];
                const ScopedShaderGraphClock pinnedClock(clock);
                // Device calls on this thread that record commands (buffer uploads) belong to this job's context.
                context.BindToCurrentThread();
                struct Unbind
                {
                    rhi::ICommandContext &context;
                    ~Unbind() { context.UnbindFromCurrentThread(); }
                } unbind{context};
                RunJob(index, context);
            });
        }
        catch (...)
        {
            m_commands.EndParallelRecording();
            m_jobs.clear();
            throw;
        }
        m_commands.EndParallelRecording();
        m_jobs.clear();
        return true;
    }
}
