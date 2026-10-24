#pragma once

#include "PlutoGE/scene/components/AnimationComponent.h"

#include <cstddef>
#include <span>
#include <vector>

namespace PlutoGE::core
{
    class TaskPool;
}

namespace PlutoGE::scene
{
    // Evaluates the poses AnimationComponent::Update left pending during a scene's component pass. Base poses (the
    // sampling, retargeting, layer blending and hierarchy that dominate character animation cost) are evaluated across
    // the task pool when one is given; resolving each mesh before them, and IK, ragdoll and timing after them, stay on
    // the calling thread. The resulting poses equal those of evaluating each component on its own.
    class AnimationPoseBatch
    {
    public:
        // Returns the number of poses evaluated.
        std::size_t Evaluate(std::span<AnimationComponent *const> animations, core::TaskPool *pool);

    private:
        struct Job
        {
            AnimationComponent *animation = nullptr;
            AnimationComponent::PoseEvaluation pose;
        };
        std::vector<Job> m_jobs;
    };
}
