#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace PlutoGE;
using namespace PlutoGE::scene;

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    // Blend-space playback rate is reference duration / blended duration, so
    // the state time after one update reveals which clips were blended.
    float AdvanceAt(float speed)
    {
        Scene scene;
        auto *owner = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Locomotion"}));
        auto *animation = owner->CreateComponent<AnimationComponent>();
        animation->SetClipsFromImportedAnimations({
            render::AnimationClip{.name = "Walk", .duration = 4.0f},
            render::AnimationClip{.name = "Run", .duration = 2.5f},
            render::AnimationClip{.name = "Sprint", .duration = 2.0f},
        });
        animation->AddParameter("Speed", AnimationComponent::AnimationParameterType::Float);
        auto &state = animation->GetGraphStates()[0];
        state.blendSpaceParameterX = "Speed";
        state.blendSpacePoints = {{0, 2.06f, 0.0f}, {1, 5.15f, 0.0f}, {2, 7.21f, 0.0f}};
        animation->SetCurrentStateIndex(0);
        animation->SetFloat("Speed", speed);
        animation->Update(0.1f);
        return animation->GetTime();
    }
}

int main()
{
    try
    {
        // Pure Run advances the Walk-referenced state time by 0.1 * 4 / 2.5.
        const float pureRun = 0.16f;
        Require(std::abs(AdvanceAt(5.15f) - pureRun) < 0.0005f, "Exact sample point did not play Run alone");
        // Just past the point must stay on the adjacent Run-Sprint segment, not
        // the Walk-Sprint segment that also passes through Run.
        const float justPast = std::nextafter(5.15f, 10.0f);
        Require(std::abs(AdvanceAt(justPast) - pureRun) < 0.0005f,
                "Collinear blend space jumped to a non-adjacent segment: " + std::to_string(AdvanceAt(justPast)));
        const float justBelow = std::nextafter(5.15f, 0.0f);
        Require(std::abs(AdvanceAt(justBelow) - pureRun) < 0.0005f, "Approaching a sample point from below changed the blend");
        // Midway between Walk and Run blends those two (duration 3.25).
        Require(std::abs(AdvanceAt(3.605f) - 0.1f * 4.0f / 3.25f) < 0.0005f, "Midpoint did not blend the adjacent pair");
        std::cout << "Animation blend space tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
