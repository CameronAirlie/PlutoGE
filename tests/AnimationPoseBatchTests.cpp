#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

// Scene::Update leaves poses pending during the component pass and evaluates them in one batch after it, sampling
// across the task pool. The batch must produce exactly the poses of evaluating each component on its own, for both
// node-hierarchy and joint-only meshes, and a pose read during the pass must still be current.
using namespace PlutoGE;
using namespace PlutoGE::scene;

namespace
{
    void Require(bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    constexpr int JointCount = 4;
    constexpr int CharacterCount = 9;

    render::Mesh MakeRig(bool withNodes)
    {
        render::MeshConfig config;
        config.skeleton.joints.resize(JointCount);
        if (withNodes) config.animationNodes.resize(JointCount);
        for (int i = 0; i < JointCount; ++i)
        {
            auto &joint = config.skeleton.joints[i];
            joint.name = "joint" + std::to_string(i);
            joint.nodeIndex = withNodes ? i : -1;
            joint.parentJointIndex = i - 1;
            joint.localBindTransform = glm::translate(glm::mat4(1), glm::vec3(i ? 1 : 0, 0, 0));
            joint.inverseBindMatrix = glm::translate(glm::mat4(1), glm::vec3(-i, 0, 0));
            if (withNodes)
                config.animationNodes[i] = {.name = joint.name, .parentNodeIndex = i - 1,
                                            .localBindTransform = joint.localBindTransform};
        }
        return render::Mesh(config);
    }

    // Rotates every joint about a different axis over two seconds.
    std::vector<render::AnimationClip> MakeClips()
    {
        render::AnimationClip clip{.name = "Wave", .duration = 2};
        for (int i = 0; i < JointCount; ++i)
        {
            render::AnimationChannel channel;
            channel.jointIndex = channel.nodeIndex = i;
            channel.targetName = "joint" + std::to_string(i);
            channel.path = render::AnimationTargetPath::Rotation;
            const glm::vec3 axis = glm::normalize(glm::vec3(1 + i, i % 2, 2 - i % 3));
            for (int key = 0; key <= 4; ++key)
            {
                const auto rotation = glm::angleAxis(0.4f * float(key) * float(i + 1), axis);
                channel.times.push_back(0.5f * float(key));
                channel.values.emplace_back(rotation.x, rotation.y, rotation.z, rotation.w);
            }
            clip.channels.push_back(std::move(channel));
        }
        clip.channelCount = JointCount;
        return {clip};
    }

    struct Crowd
    {
        Scene scene;
        std::vector<AnimationComponent *> animations;

        Crowd(const render::Mesh &mesh, const std::vector<render::AnimationClip> &clips)
        {
            for (int index = 0; index < CharacterCount; ++index)
            {
                auto *owner = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Character"}));
                auto *animation = owner->CreateComponent<AnimationComponent>();
                animation->SetClipsFromImportedAnimations(clips);
                animation->SetSpeed(0.5f + 0.25f * float(index));
                animation->Seek(0.13f * float(index));
                animation->Play();
                auto *body = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Body"}), owner);
                body->CreateComponent<MeshComponent>(MeshComponentConfig{.mesh = const_cast<render::Mesh *>(&mesh)});
                animations.push_back(animation);
            }
        }

        std::vector<std::vector<glm::mat4>> Poses(const render::Mesh &mesh)
        {
            std::vector<std::vector<glm::mat4>> poses;
            for (auto *animation : animations)
                poses.push_back(animation->GetJointMatrices(mesh.GetSkeleton(), mesh.GetAnimationNodes()));
            return poses;
        }
    };

    void BatchMatchesIndividualEvaluation(bool withNodes)
    {
        const auto mesh = MakeRig(withNodes);
        const auto clips = MakeClips();
        Crowd parallel(mesh, clips), serial(mesh, clips), individual(mesh, clips);
        serial.scene.SetParallelAnimationEnabled(false);
        for (int frame = 0; frame < 12; ++frame)
        {
            const float delta = 1.0f / 60.0f + 0.003f * float(frame % 3);
            parallel.scene.Update(delta);
            serial.scene.Update(delta);
            // Outside Scene::Update a component evaluates its pose itself.
            for (auto *animation : individual.animations) animation->Update(delta);
            for (auto *animation : parallel.animations)
                Require(!animation->IsPoseEvaluationPending(), "a pose was left pending after the update");
            Require(parallel.scene.GetUpdateTimingStats().animationPoseCount == CharacterCount,
                    "the batch did not evaluate every character");
            const auto expected = individual.Poses(mesh);
            Require(parallel.Poses(mesh) == expected, "parallel batched poses differ from individual evaluation");
            Require(serial.Poses(mesh) == expected, "serial batched poses differ from individual evaluation");
        }
    }

    // A component reading another's pose during the pass gets the new pose, evaluated on demand.
    void PoseReadDuringPassIsCurrent()
    {
        const auto mesh = MakeRig(true);
        Crowd crowd(mesh, MakeClips());
        crowd.scene.Update(0.25f);
        auto *animation = crowd.animations.front();
        const auto before = animation->GetJointMatrices(mesh.GetSkeleton(), mesh.GetAnimationNodes());
        // Simulate the pass: Update leaves the pose pending only while the scene batches.
        animation->Update(0.25f);
        Require(!animation->IsPoseEvaluationPending(), "a component updated outside the pass deferred its pose");
        const auto after = animation->GetJointMatrices(mesh.GetSkeleton(), mesh.GetAnimationNodes());
        Require(after != before, "the pose did not advance");
    }
}

int main()
{
    try
    {
        BatchMatchesIndividualEvaluation(true);
        BatchMatchesIndividualEvaluation(false);
        PoseReadDuringPassIsCurrent();
        std::cout << "Animation pose batch tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
