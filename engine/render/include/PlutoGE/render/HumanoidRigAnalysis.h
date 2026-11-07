#pragma once

#include "PlutoGE/render/Mesh.h"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

// Hierarchy checks for a humanoid map. Bone names cannot settle these: rigs number
// their spines in either direction, and a pelvis may have any name or none at all.
namespace PlutoGE::render
{
    inline bool IsValidSkeletonJoint(const Skeleton &skeleton, int joint)
    {
        return joint >= 0 && joint < static_cast<int>(skeleton.joints.size());
    }

    inline bool IsSkeletonJointAncestor(const Skeleton &skeleton, int ancestor, int joint)
    {
        if (!IsValidSkeletonJoint(skeleton, ancestor) || !IsValidSkeletonJoint(skeleton, joint))
            return false;
        int current = joint;
        for (std::size_t depth = 0; depth < skeleton.joints.size(); ++depth)
        {
            current = skeleton.joints[static_cast<std::size_t>(current)].parentJointIndex;
            if (!IsValidSkeletonJoint(skeleton, current))
                return false;
            if (current == ancestor)
                return true;
        }
        return false;
    }

    inline int GetHumanoidTargetJoint(const Skeleton &skeleton, HumanoidBone bone)
    {
        for (const auto &mapping : skeleton.humanoidBoneMappings)
            if (mapping.bone == bone && IsValidSkeletonJoint(skeleton, mapping.targetJointIndex))
                return mapping.targetJointIndex;
        return -1;
    }

    // Reassigns the joints already chosen for Spine/Chest/UpperChest from the bottom of the
    // chain upward. MakeHuman numbers spine01 at the top, so name matching maps it backwards.
    // Returns whether anything changed; joints that do not form one chain are left alone.
    inline bool CorrectHumanoidSpineOrder(Skeleton &skeleton)
    {
        std::vector<HumanoidBoneMapping *> slots;
        for (const auto bone : {HumanoidBone::Spine, HumanoidBone::Chest, HumanoidBone::UpperChest})
            for (auto &mapping : skeleton.humanoidBoneMappings)
                if (mapping.bone == bone && IsValidSkeletonJoint(skeleton, mapping.targetJointIndex))
                    slots.push_back(&mapping);
        if (slots.size() < 2)
            return false;

        std::vector<int> joints;
        for (const auto *slot : slots)
            joints.push_back(slot->targetJointIndex);
        std::sort(joints.begin(), joints.end(), [&](int a, int b) { return IsSkeletonJointAncestor(skeleton, a, b); });
        for (std::size_t index = 1; index < joints.size(); ++index)
            if (!IsSkeletonJointAncestor(skeleton, joints[index - 1], joints[index]))
                return false;

        bool changed = false;
        for (std::size_t index = 0; index < slots.size(); ++index)
        {
            changed = changed || slots[index]->targetJointIndex != joints[index];
            slots[index]->targetJointIndex = joints[index];
        }
        return changed;
    }

    // The joint that carries both the upper body and the legs. A rig with no such joint
    // (separate roots for the torso and each leg) falls back to the root above the head.
    inline int InferHumanoidHipsJoint(const Skeleton &skeleton)
    {
        const int head = GetHumanoidTargetJoint(skeleton, HumanoidBone::Head);
        if (head < 0)
            return -1;
        const int left = GetHumanoidTargetJoint(skeleton, HumanoidBone::LeftUpperLeg);
        const int right = GetHumanoidTargetJoint(skeleton, HumanoidBone::RightUpperLeg);
        int top = head;
        for (std::size_t depth = 0; depth < skeleton.joints.size(); ++depth)
        {
            const int parent = skeleton.joints[static_cast<std::size_t>(top)].parentJointIndex;
            if (!IsValidSkeletonJoint(skeleton, parent))
                break;
            top = parent;
            if (left >= 0 && right >= 0 && IsSkeletonJointAncestor(skeleton, top, left) && IsSkeletonJointAncestor(skeleton, top, right))
                return top;
        }
        return top == head ? -1 : top;
    }

    // Plain-language problems a "Rig Ready" count cannot show: every required bone can be
    // assigned and the character still animates wrongly if the order contradicts the hierarchy.
    inline std::vector<std::string> FindHumanoidRigProblems(const Skeleton &skeleton)
    {
        using Bone = HumanoidBone;
        std::vector<std::string> problems;
        if (skeleton.humanoidBoneMappings.empty())
            return problems;
        if (GetHumanoidTargetJoint(skeleton, Bone::Hips) < 0)
            problems.emplace_back("Hips is unassigned, so the body cannot follow the animation's pelvis. Choose the joint above the spine.");

        constexpr std::array<std::array<Bone, 6>, 5> chains = {{
            {Bone::Hips, Bone::Spine, Bone::Chest, Bone::UpperChest, Bone::Neck, Bone::Head},
            {Bone::LeftShoulder, Bone::LeftUpperArm, Bone::LeftLowerArm, Bone::LeftHand, Bone::Count, Bone::Count},
            {Bone::RightShoulder, Bone::RightUpperArm, Bone::RightLowerArm, Bone::RightHand, Bone::Count, Bone::Count},
            {Bone::LeftUpperLeg, Bone::LeftLowerLeg, Bone::LeftFoot, Bone::LeftToes, Bone::Count, Bone::Count},
            {Bone::RightUpperLeg, Bone::RightLowerLeg, Bone::RightFoot, Bone::RightToes, Bone::Count, Bone::Count},
        }};
        for (const auto &chain : chains)
        {
            Bone previous = Bone::Count;
            for (const Bone bone : chain)
            {
                if (bone == Bone::Count)
                    break;
                const int joint = GetHumanoidTargetJoint(skeleton, bone);
                if (joint < 0)
                    continue;
                if (previous != Bone::Count)
                {
                    const int above = GetHumanoidTargetJoint(skeleton, previous);
                    if (above != joint && !IsSkeletonJointAncestor(skeleton, above, joint))
                        problems.push_back(std::string(HumanoidBoneName(previous)) + " (" + skeleton.joints[static_cast<std::size_t>(above)].name +
                                           ") must be above " + HumanoidBoneName(bone) + " (" + skeleton.joints[static_cast<std::size_t>(joint)].name +
                                           ") in the skeleton.");
                }
                previous = bone;
            }
        }
        return problems;
    }
}
