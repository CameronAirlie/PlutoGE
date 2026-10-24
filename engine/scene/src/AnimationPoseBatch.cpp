#include "AnimationPoseBatch.h"

#include "PlutoGE/core/TaskPool.h"

namespace PlutoGE::scene
{
    std::size_t AnimationPoseBatch::Evaluate(std::span<AnimationComponent *const> animations, core::TaskPool *pool)
    {
        m_jobs.clear();
        for (auto *animation : animations)
            if (animation && animation->IsPoseEvaluationPending())
                if (const auto pose = animation->PreparePoseEvaluation())
                    m_jobs.push_back({animation, *pose});

        const auto evaluate = [&](std::size_t index) { m_jobs[index].animation->EvaluateBasePose(m_jobs[index].pose); };
        if (pool && m_jobs.size() > 1)
            pool->Run(m_jobs.size(), evaluate);
        else
            for (std::size_t index = 0; index < m_jobs.size(); ++index)
                evaluate(index);

        for (const auto &job : m_jobs)
            job.animation->FinishPoseEvaluation(job.pose);
        return m_jobs.size();
    }
}
