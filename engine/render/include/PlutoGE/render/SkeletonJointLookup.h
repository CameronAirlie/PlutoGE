#pragma once
#include "PlutoGE/render/Mesh.h"

namespace PlutoGE::render
{
    // Canonical key for conventional finger joints (Blender, Unreal and Mixamo).
    // Core humanoid joints use saved mappings instead. Unknown names stay exact-only.
    inline std::string HumanoidFingerKey(std::string_view name)
    {
        std::string n = NormalizeHumanoidBoneName(name), side;
        if (n.starts_with("left")) { side = "left"; n.erase(0, 4); }
        else if (n.starts_with("right")) { side = "right"; n.erase(0, 5); }
        else if (!n.empty() && (n.back() == 'l' || n.back() == 'r'))
        { side = n.back() == 'l' ? "left" : "right"; n.pop_back(); }
        else return {};
        if (n.starts_with("hand")) n.erase(0, 4);
        for (const std::string_view finger : {"thumb", "index", "middle", "ring", "pinky", "little"})
        {
            if (!n.starts_with(finger)) continue;
            auto segment = std::string_view(n).substr(finger.size());
            while (segment.size() > 1 && segment.front() == '0') segment.remove_prefix(1);
            if (segment.size() != 1 || segment.front() < '1' || segment.front() > '4') return {};
            return side + std::string(finger == "little" ? "pinky" : finger) + std::string(segment);
        }
        return {};
    }

    // One resolver for pose queries, constraints and attachments. Exact names preserve
    // existing rigs; semantic aliases honor authored mappings, and ambiguous fallbacks fail.
    inline int FindSkeletonJoint(const Skeleton &skeleton, std::string_view name)
    {
        if (name.empty()) return -1;
        for (size_t i = 0; i < skeleton.joints.size(); ++i)
            if (skeleton.joints[i].name == name) return static_cast<int>(i);
        if (const auto bone = GuessHumanoidBone(name))
        {
            int mapped = -1;
            for (const auto &mapping : skeleton.humanoidBoneMappings)
            {
                if (mapping.bone != *bone || mapping.targetJointIndex < 0 ||
                    mapping.targetJointIndex >= static_cast<int>(skeleton.joints.size())) continue;
                if (mapped >= 0 && mapped != mapping.targetJointIndex) return -1;
                mapped = mapping.targetJointIndex;
            }
            if (mapped >= 0) return mapped;
        }
        const auto normalized = NormalizeHumanoidBoneName(name);
        const auto finger = HumanoidFingerKey(name);
        int found = -1;
        for (size_t i = 0; i < skeleton.joints.size(); ++i)
        {
            const auto &candidate = skeleton.joints[i].name;
            if (NormalizeHumanoidBoneName(candidate) != normalized &&
                (finger.empty() || HumanoidFingerKey(candidate) != finger)) continue;
            if (found >= 0) return -1;
            found = static_cast<int>(i);
        }
        return found;
    }
}
