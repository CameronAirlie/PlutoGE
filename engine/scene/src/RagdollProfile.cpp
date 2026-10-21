#include "PlutoGE/scene/RagdollProfile.h"

#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/components/RagdollTuningComponent.h"
#include <algorithm>
#include <cmath>

namespace PlutoGE::scene
{
    namespace
    {
        using render::HumanoidBone;

        RagdollJointSettings Joint(RagdollFlexDirection flex, float flexMin, float flexMax, float swing, float twist,
                                   float massFraction, float strength, float radius = 0.0f)
        {
            return {.limits = {.twist = twist, .swing = swing, .flexMin = flexMin, .flexMax = flexMax},
                    .flexDirection = flex, .massFraction = massFraction, .strength = strength, .radius = radius};
        }

        void Set(RagdollProfile &profile, std::initializer_list<HumanoidBone> bones, const RagdollJointSettings &settings,
                 float brace = 1.0f)
        {
            for (const auto bone : bones)
            {
                profile.bones[static_cast<size_t>(bone)] = settings;
                profile.bones[static_cast<size_t>(bone)].brace = brace;
            }
        }

        float SafeLength(const glm::vec3 &value) { return std::sqrt(glm::dot(value, value)); }
    }

    float RagdollMuscleTone::At(float seconds) const
    {
        if (seconds <= holdSeconds)
            return initial;
        if (decaySeconds <= 0.0f)
            return sustained;
        const float t = std::clamp((seconds - holdSeconds) / decaySeconds, 0.0f, 1.0f);
        const float eased = t * t * (3.0f - 2.0f * t);
        return initial + (sustained - initial) * eased;
    }

    RagdollProfile RagdollProfile::Humanoid()
    {
        // Ranges are anatomical (degrees from a T- or A-pose bind); masses
        // follow segment mass tables (they are renormalised over the bones a
        // rig has). Strength 1 holds the limb level against gravity: the trunk,
        // neck and arms brace for a moment; the legs barely brace and cannot
        // carry the body, so it folds at the hips and knees as it falls.
        using F = RagdollFlexDirection;
        RagdollProfile profile;
        profile.other = Joint(F::Forward, -30, 30, 30, 20, 0.02f, 0.6f);
        profile.bones.fill(profile.other);
        Set(profile, {HumanoidBone::Hips}, Joint(F::Forward, 0, 0, 0, 0, 0.11f, 0.0f, 0.13f)); // The root: no joint.
        Set(profile, {HumanoidBone::Spine}, Joint(F::Forward, -20, 35, 15, 12, 0.10f, 1.0f, 0.12f));
        Set(profile, {HumanoidBone::Chest}, Joint(F::Forward, -15, 25, 12, 12, 0.10f, 1.0f, 0.13f));
        Set(profile, {HumanoidBone::UpperChest}, Joint(F::Forward, -10, 20, 10, 10, 0.08f, 1.0f, 0.13f));
        Set(profile, {HumanoidBone::Neck}, Joint(F::Forward, -30, 40, 25, 35, 0.02f, 1.2f, 0.05f));
        Set(profile, {HumanoidBone::Head}, Joint(F::Forward, -30, 35, 20, 35, 0.07f, 1.2f, 0.1f));
        Set(profile, {HumanoidBone::LeftShoulder, HumanoidBone::RightShoulder}, Joint(F::Up, -10, 25, 15, 10, 0.015f, 1.2f));
        // From a T-pose the arm hangs at +90 (flexion is down); swing is forward/back.
        Set(profile, {HumanoidBone::LeftUpperArm, HumanoidBone::RightUpperArm}, Joint(F::Down, -60, 95, 80, 60, 0.027f, 1.1f));
        Set(profile, {HumanoidBone::LeftLowerArm, HumanoidBone::RightLowerArm}, Joint(F::Forward, -5, 145, 5, 60, 0.016f, 1.0f));
        Set(profile, {HumanoidBone::LeftHand, HumanoidBone::RightHand}, Joint(F::Down, -60, 70, 25, 20, 0.006f, 0.8f));
        Set(profile, {HumanoidBone::LeftUpperLeg, HumanoidBone::RightUpperLeg}, Joint(F::Forward, -20, 110, 40, 30, 0.10f, 0.8f), 0.15f);
        Set(profile, {HumanoidBone::LeftLowerLeg, HumanoidBone::RightLowerLeg}, Joint(F::Backward, -2, 140, 4, 8, 0.045f, 1.0f), 0.15f);
        Set(profile, {HumanoidBone::LeftFoot, HumanoidBone::RightFoot}, Joint(F::Up, -45, 25, 20, 10, 0.014f, 0.8f), 0.15f);
        Set(profile, {HumanoidBone::LeftToes, HumanoidBone::RightToes}, Joint(F::Up, -30, 40, 5, 5, 0.004f, 0.6f), 0.15f);
        return profile;
    }

    const RagdollJointSettings &RagdollProfile::For(std::optional<render::HumanoidBone> bone) const
    {
        return bone && static_cast<size_t>(*bone) < bones.size() ? bones[static_cast<size_t>(*bone)] : other;
    }

    RagdollProfile ResolveRagdollProfile(const RagdollTuningComponent *tuning)
    {
        auto profile = RagdollProfile::Humanoid();
        ApplyRagdollTuning(profile, tuning);
        return profile;
    }

    void ApplyRagdollTuning(RagdollProfile &profile, const RagdollTuningComponent *tuning)
    {
        if (!tuning || !tuning->IsEnabled())
            return;
        profile.bodyMass = tuning->GetBodyMass();
        profile.strength = tuning->GetMuscleStrength();
        profile.damping = tuning->GetJointDamping();
        profile.friction = tuning->GetJointFriction();
        profile.tone = {.initial = tuning->GetInitialTone(), .sustained = tuning->GetSustainedTone(),
                        .holdSeconds = tuning->GetToneHold(), .decaySeconds = tuning->GetToneDecay()};
        profile.anatomicalLimits = tuning->GetAnatomicalLimits();
    }

    std::vector<std::optional<render::HumanoidBone>> ClassifyRagdollJoints(const render::Skeleton &skeleton)
    {
        std::vector<std::optional<render::HumanoidBone>> bones(skeleton.joints.size());
        for (const auto &mapping : skeleton.humanoidBoneMappings)
            if (mapping.targetJointIndex >= 0 && mapping.targetJointIndex < static_cast<int>(bones.size()))
                bones[static_cast<size_t>(mapping.targetJointIndex)] = mapping.bone;
        for (size_t joint = 0; joint < bones.size(); ++joint)
            if (!bones[joint])
                bones[joint] = render::GuessHumanoidBone(skeleton.joints[joint].name);
        return bones;
    }

    glm::vec3 RagdollBodyAxes::Direction(RagdollFlexDirection direction) const
    {
        switch (direction)
        {
        case RagdollFlexDirection::Forward: return forward;
        case RagdollFlexDirection::Backward: return -forward;
        case RagdollFlexDirection::Up: return up;
        case RagdollFlexDirection::Down: return -up;
        }
        return forward;
    }

    RagdollBodyAxes EstimateRagdollBodyAxes(std::span<const std::optional<render::HumanoidBone>> bones,
                                            std::span<const glm::vec3> bindPositions)
    {
        const auto find = [&](std::initializer_list<HumanoidBone> candidates) -> std::optional<glm::vec3>
        {
            for (const auto candidate : candidates)
                for (size_t joint = 0; joint < bones.size() && joint < bindPositions.size(); ++joint)
                    if (bones[joint] == candidate)
                        return bindPositions[joint];
            return std::nullopt;
        };
        RagdollBodyAxes axes;
        const auto hips = find({HumanoidBone::Hips});
        const auto top = find({HumanoidBone::Head, HumanoidBone::Neck, HumanoidBone::UpperChest, HumanoidBone::Chest});
        if (hips && top && SafeLength(*top - *hips) > 1e-4f)
            axes.up = glm::normalize(*top - *hips);

        auto left = find({HumanoidBone::LeftUpperLeg, HumanoidBone::LeftUpperArm, HumanoidBone::LeftShoulder});
        auto right = find({HumanoidBone::RightUpperLeg, HumanoidBone::RightUpperArm, HumanoidBone::RightShoulder});
        glm::vec3 forward = axes.forward;
        if (left && right && SafeLength(*right - *left) > 1e-4f)
            forward = glm::cross(axes.up, glm::normalize(*right - *left)); // Facing +Z, a character's right is -X.
        // Toes point forward: they settle the sign if left and right are swapped.
        const auto foot = find({HumanoidBone::LeftFoot, HumanoidBone::RightFoot});
        const auto toes = find({HumanoidBone::LeftToes, HumanoidBone::RightToes});
        if (foot && toes && glm::dot(*toes - *foot, forward) < 0.0f)
            forward = -forward;
        forward -= axes.up * glm::dot(forward, axes.up);
        if (SafeLength(forward) > 1e-4f)
            axes.forward = glm::normalize(forward);
        else
            axes.forward = std::abs(axes.up.z) < 0.9f ? glm::normalize(glm::cross(glm::vec3(1, 0, 0), axes.up))
                                                     : glm::vec3(1, 0, 0);
        return axes;
    }

    glm::mat3 RagdollJointBasis(const glm::vec3 &boneDirection, const glm::vec3 &flexDirection)
    {
        const glm::vec3 twist = SafeLength(boneDirection) > 1e-6f ? glm::normalize(boneDirection) : glm::vec3(0, 1, 0);
        glm::vec3 flex = flexDirection - twist * glm::dot(flexDirection, twist);
        if (SafeLength(flex) < 1e-3f)
        {
            const glm::vec3 helper = std::abs(twist.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 0, 1);
            flex = helper - twist * glm::dot(helper, twist);
        }
        flex = glm::normalize(flex);
        return glm::mat3(twist, flex, glm::cross(twist, flex));
    }
}
