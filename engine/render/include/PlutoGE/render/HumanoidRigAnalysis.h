#pragma once

#include "PlutoGE/render/HumanoidRig.h"
#include "PlutoGE/render/Mesh.h"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

// Hierarchy checks and automatic mapping for a humanoid map. Bone names cannot settle
// these: rigs number their spines in either direction, and a pelvis may have any name,
// a joint per leg ("pelvis.L", "pelvis.R"), or none at all.
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

    // The topmost ancestor of `joint` (the joint itself when it is a root); -1 for an invalid joint.
    inline int GetSkeletonRootJoint(const Skeleton &skeleton, int joint)
    {
        if (!IsValidSkeletonJoint(skeleton, joint))
            return -1;
        for (std::size_t depth = 0; depth < skeleton.joints.size(); ++depth)
        {
            const int parent = skeleton.joints[static_cast<std::size_t>(joint)].parentJointIndex;
            if (!IsValidSkeletonJoint(skeleton, parent))
                break;
            joint = parent;
        }
        return joint;
    }

    inline int GetHumanoidTargetJoint(const Skeleton &skeleton, HumanoidBone bone)
    {
        for (const auto &mapping : skeleton.humanoidBoneMappings)
            if (mapping.bone == bone && IsValidSkeletonJoint(skeleton, mapping.targetJointIndex))
                return mapping.targetJointIndex;
        return -1;
    }

    // Bones a humanoid map needs before clips can drive the body. The spine and chest slots are
    // optional: a rig with a single torso bone has nothing to put in them.
    constexpr bool IsRequiredHumanoidBone(HumanoidBone bone)
    {
        switch (bone)
        {
        case HumanoidBone::Hips:
        case HumanoidBone::Head:
        case HumanoidBone::LeftUpperArm:
        case HumanoidBone::LeftLowerArm:
        case HumanoidBone::LeftHand:
        case HumanoidBone::RightUpperArm:
        case HumanoidBone::RightLowerArm:
        case HumanoidBone::RightHand:
        case HumanoidBone::LeftUpperLeg:
        case HumanoidBone::LeftLowerLeg:
        case HumanoidBone::LeftFoot:
        case HumanoidBone::RightUpperLeg:
        case HumanoidBone::RightLowerLeg:
        case HumanoidBone::RightFoot:
            return true;
        default:
            return false;
        }
    }

    inline HumanoidBoneMapping *FindHumanoidMapping(Skeleton &skeleton, HumanoidBone bone)
    {
        for (auto &mapping : skeleton.humanoidBoneMappings)
            if (mapping.bone == bone)
                return &mapping;
        return nullptr;
    }

    inline const HumanoidBoneMapping *FindHumanoidMapping(const Skeleton &skeleton, HumanoidBone bone)
    {
        for (const auto &mapping : skeleton.humanoidBoneMappings)
            if (mapping.bone == bone)
                return &mapping;
        return nullptr;
    }

    // Gives the map exactly one slot per humanoid bone, keeping the choices already made.
    // New slots start unassigned; Hips copies translation so the body can travel.
    inline void EnsureHumanoidMappings(Skeleton &skeleton)
    {
        std::array<bool, kHumanoidBoneCount> seen{};
        std::erase_if(skeleton.humanoidBoneMappings, [&](const HumanoidBoneMapping &mapping)
                      {
                          const auto index = static_cast<std::size_t>(mapping.bone);
                          if (index >= kHumanoidBoneCount || seen[index])
                              return true;
                          seen[index] = true;
                          return false;
                      });
        for (std::size_t index = 0; index < kHumanoidBoneCount; ++index)
        {
            if (seen[index])
                continue;
            const auto bone = static_cast<HumanoidBone>(index);
            skeleton.humanoidBoneMappings.push_back({.bone = bone, .copyTranslation = bone == HumanoidBone::Hips});
        }
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

    inline bool IsHumanoidJointMapped(const Skeleton &skeleton, int joint)
    {
        return std::ranges::any_of(skeleton.humanoidBoneMappings, [&](const auto &mapping) { return mapping.targetJointIndex == joint; });
    }

    // Blender's Rigify names its torso by number: "spine" is the pelvis, spine.001-.003 the torso,
    // spine.004 the neck and spine.006 the head, with "pelvis.L"/"pelvis.R" side bones that take no
    // slot. Generic names would put the pelvis in Spine. Fills the empty torso slots from that
    // numbering when the rig follows it; returns whether it did.
    inline bool AutoMapRigifyTorso(Skeleton &skeleton)
    {
        std::array<int, 7> spine; // spine, spine.001 ... spine.006
        spine.fill(-1);
        for (std::size_t index = 0; index < skeleton.joints.size(); ++index)
        {
            const std::string name = NormalizeHumanoidBoneName(skeleton.joints[index].name);
            if (name == "spine")
                spine[0] = static_cast<int>(index);
            else if (name.size() == 8 && name.starts_with("spine00") && name[7] >= '1' && name[7] <= '6')
                spine[static_cast<std::size_t>(name[7] - '0')] = static_cast<int>(index);
        }
        // The numbering must run up one chain from the pelvis to the neck.
        for (std::size_t index = 1; index <= 4; ++index)
            if (spine[index] < 0 || !IsSkeletonJointAncestor(skeleton, spine[index - 1], spine[index]))
                return false;
        const int head = spine[6] >= 0 ? spine[6] : spine[5];
        const std::array<std::pair<HumanoidBone, int>, 6> slots = {{
            {HumanoidBone::Hips, spine[0]},
            {HumanoidBone::Spine, spine[1]},
            {HumanoidBone::Chest, spine[2]},
            {HumanoidBone::UpperChest, spine[3]},
            {HumanoidBone::Neck, spine[4]},
            {HumanoidBone::Head, head},
        }};
        for (const auto &[bone, joint] : slots)
            if (auto *mapping = FindHumanoidMapping(skeleton, bone); mapping && mapping->targetJointIndex < 0 && joint >= 0 &&
                                                                    !IsHumanoidJointMapped(skeleton, joint))
                mapping->targetJointIndex = joint;
        return true;
    }

    // Fills unassigned slots from joint names (Mixamo, Unreal, Blender/Rigify and common aliases),
    // orders the spine slots by depth and, when no joint is named like a pelvis, takes Hips from
    // the hierarchy. Slots that already have a joint keep it, and a joint fills one slot at most.
    inline void AutoMapHumanoidRig(Skeleton &skeleton)
    {
        EnsureHumanoidMappings(skeleton);
        AutoMapRigifyTorso(skeleton);
        for (std::size_t jointIndex = 0; jointIndex < skeleton.joints.size(); ++jointIndex)
        {
            const auto bone = GuessHumanoidBone(skeleton.joints[jointIndex].name);
            if (!bone)
                continue;
            const int joint = static_cast<int>(jointIndex);
            if (auto *mapping = FindHumanoidMapping(skeleton, *bone); mapping && mapping->targetJointIndex < 0 && !IsHumanoidJointMapped(skeleton, joint))
                mapping->targetJointIndex = joint;
        }

        CorrectHumanoidSpineOrder(skeleton);
        auto *hips = FindHumanoidMapping(skeleton, HumanoidBone::Hips);
        if (!hips || hips->targetJointIndex >= 0)
            return;
        const int inferred = InferHumanoidHipsJoint(skeleton);
        if (inferred < 0)
            return;
        // The joint carrying both legs is the pelvis whatever it is called. When its name put it in
        // a spine slot ("spine" on Blender rigs), it moves to Hips and the spine slots move down.
        std::array<HumanoidBoneMapping *, 3> spine = {FindHumanoidMapping(skeleton, HumanoidBone::Spine),
                                                      FindHumanoidMapping(skeleton, HumanoidBone::Chest),
                                                      FindHumanoidMapping(skeleton, HumanoidBone::UpperChest)};
        for (std::size_t slot = 0; slot < spine.size(); ++slot)
        {
            if (!spine[slot] || spine[slot]->targetJointIndex != inferred)
                continue;
            for (std::size_t next = slot; next + 1 < spine.size(); ++next)
                if (spine[next] && spine[next + 1])
                    spine[next]->targetJointIndex = spine[next + 1]->targetJointIndex;
            if (spine.back())
                spine.back()->targetJointIndex = -1;
            break;
        }
        if (!IsHumanoidJointMapped(skeleton, inferred))
            hips->targetJointIndex = inferred;
    }

    // Split pelvis: a rig that hangs its torso and each leg from separate skeleton roots
    // ("Torso", "pelvis.L", "pelvis.R") has no joint carrying the whole body. Hips goes on the
    // torso root, and the other roots above mapped joints follow it rigidly. Returns those
    // follower roots; empty when Hips is unassigned or is not itself a root.
    inline std::vector<int> FindHumanoidHipsFollowers(const Skeleton &skeleton)
    {
        std::vector<int> followers;
        const int hips = GetHumanoidTargetJoint(skeleton, HumanoidBone::Hips);
        if (hips < 0 || IsValidSkeletonJoint(skeleton, skeleton.joints[static_cast<std::size_t>(hips)].parentJointIndex))
            return followers;
        const auto isMapped = [&](int joint)
        {
            return std::ranges::any_of(skeleton.humanoidBoneMappings, [&](const auto &mapping) { return mapping.targetJointIndex == joint; });
        };
        for (const auto &mapping : skeleton.humanoidBoneMappings)
        {
            const int top = GetSkeletonRootJoint(skeleton, mapping.targetJointIndex);
            if (top >= 0 && top != hips && !isMapped(top) && std::ranges::find(followers, top) == followers.end())
                followers.push_back(top);
        }
        return followers;
    }

    struct HumanoidRigProblem
    {
        HumanoidBone bone = HumanoidBone::Count; // the slot to fix, Count for the whole map
        std::string message;
    };

    // Plain-language problems a "Rig Ready" count cannot show: every required bone can be
    // assigned and the character still animates wrongly if the order contradicts the hierarchy.
    inline std::vector<HumanoidRigProblem> AnalyzeHumanoidRigProblems(const Skeleton &skeleton)
    {
        using Bone = HumanoidBone;
        std::vector<HumanoidRigProblem> problems;
        if (skeleton.humanoidBoneMappings.empty())
            return problems;
        const auto jointName = [&](int joint) { return skeleton.joints[static_cast<std::size_t>(joint)].name; };

        const int hips = GetHumanoidTargetJoint(skeleton, Bone::Hips);
        bool hipsOnOneSide = false;
        if (hips < 0)
        {
            problems.push_back({Bone::Hips, "Hips is unassigned, so the body cannot follow the animation's pelvis. Choose the joint above the spine."});
        }
        else
        {
            // One side of a split pelvis ("pelvis.L") carries one leg, so it cannot be the hips.
            const int left = GetHumanoidTargetJoint(skeleton, Bone::LeftUpperLeg);
            const int right = GetHumanoidTargetJoint(skeleton, Bone::RightUpperLeg);
            const auto carries = [&](int leg) { return leg == hips || IsSkeletonJointAncestor(skeleton, hips, leg); };
            hipsOnOneSide = left >= 0 && right >= 0 && carries(left) != carries(right);
            if (hipsOnOneSide)
            {
                const int torso = InferHumanoidHipsJoint(skeleton);
                std::string message = "Hips (" + jointName(hips) + ") carries only the " + (carries(left) ? "left" : "right") +
                                      " leg: this rig splits its pelvis into a joint per leg. Map Hips to the joint above the spine";
                if (torso >= 0 && torso != hips)
                    message += " (" + jointName(torso) + ")";
                message += "; the leg roots then follow it.";
                problems.push_back({Bone::Hips, std::move(message)});
            }
        }

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
                // The split-pelvis message above already explains a one-sided Hips.
                if (previous != Bone::Count && !(previous == Bone::Hips && hipsOnOneSide))
                {
                    const int above = GetHumanoidTargetJoint(skeleton, previous);
                    if (above != joint && !IsSkeletonJointAncestor(skeleton, above, joint))
                        problems.push_back({bone, std::string(HumanoidBoneName(previous)) + " (" + jointName(above) + ") must be above " +
                                                      HumanoidBoneName(bone) + " (" + jointName(joint) + ") in the skeleton."});
                }
                previous = bone;
            }
        }

        // A joint follows one bone only; a second slot on it would fight the first.
        for (std::size_t index = 0; index < kHumanoidBoneCount; ++index)
        {
            const auto bone = static_cast<Bone>(index);
            const int joint = GetHumanoidTargetJoint(skeleton, bone);
            if (joint < 0)
                continue;
            for (std::size_t earlier = 0; earlier < index; ++earlier)
            {
                const auto other = static_cast<Bone>(earlier);
                if (GetHumanoidTargetJoint(skeleton, other) == joint)
                {
                    problems.push_back({bone, std::string(HumanoidBoneName(other)) + " and " + HumanoidBoneName(bone) + " both use " +
                                                  jointName(joint) + ". A joint can follow one bone only."});
                    break;
                }
            }
        }
        return problems;
    }

    inline std::vector<std::string> FindHumanoidRigProblems(const Skeleton &skeleton)
    {
        std::vector<std::string> messages;
        for (auto &problem : AnalyzeHumanoidRigProblems(skeleton))
            messages.push_back(std::move(problem.message));
        return messages;
    }

    // Everything the rig editor reports about a humanoid map.
    struct HumanoidRigStatus
    {
        int requiredMapped = 0;
        int requiredCount = 0;
        std::vector<HumanoidRigProblem> problems;
        std::vector<int> hipsFollowers; // split-pelvis roots that move with Hips

        [[nodiscard]] bool IsReady() const { return requiredMapped == requiredCount && problems.empty(); }
    };

    inline HumanoidRigStatus AnalyzeHumanoidRig(const Skeleton &skeleton)
    {
        HumanoidRigStatus status;
        for (std::size_t index = 0; index < kHumanoidBoneCount; ++index)
        {
            const auto bone = static_cast<HumanoidBone>(index);
            if (!IsRequiredHumanoidBone(bone))
                continue;
            ++status.requiredCount;
            if (GetHumanoidTargetJoint(skeleton, bone) >= 0)
                ++status.requiredMapped;
        }
        status.problems = AnalyzeHumanoidRigProblems(skeleton);
        status.hipsFollowers = FindHumanoidHipsFollowers(skeleton);
        return status;
    }
}
