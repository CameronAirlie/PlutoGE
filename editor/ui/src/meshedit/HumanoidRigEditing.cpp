#include "PlutoGE/ui/meshedit/HumanoidRigEditing.h"

#include "PlutoGE/render/HumanoidRigAnalysis.h"

#include <array>
#include <cctype>

namespace PlutoGE::ui::meshedit
{
    namespace
    {
        using Bone = render::HumanoidBone;

        constexpr std::array kBodyBones = {Bone::Hips, Bone::Spine, Bone::Chest, Bone::UpperChest, Bone::Neck, Bone::Head};
        constexpr std::array kLeftArmBones = {Bone::LeftShoulder, Bone::LeftUpperArm, Bone::LeftLowerArm, Bone::LeftHand};
        constexpr std::array kRightArmBones = {Bone::RightShoulder, Bone::RightUpperArm, Bone::RightLowerArm, Bone::RightHand};
        constexpr std::array kLeftLegBones = {Bone::LeftUpperLeg, Bone::LeftLowerLeg, Bone::LeftFoot, Bone::LeftToes};
        constexpr std::array kRightLegBones = {Bone::RightUpperLeg, Bone::RightLowerLeg, Bone::RightFoot, Bone::RightToes};

        const std::array<HumanoidBodyPartBones, 5> kBodyParts = {{
            {HumanoidBodyPart::Body, "Body", kBodyBones},
            {HumanoidBodyPart::LeftArm, "Left Arm", kLeftArmBones},
            {HumanoidBodyPart::RightArm, "Right Arm", kRightArmBones},
            {HumanoidBodyPart::LeftLeg, "Left Leg", kLeftLegBones},
            {HumanoidBodyPart::RightLeg, "Right Leg", kRightLegBones},
        }};

        // Every bone in list order.
        const std::array<Bone, render::kHumanoidBoneCount> &ListOrder()
        {
            static const auto order = []
            {
                std::array<Bone, render::kHumanoidBoneCount> bones{};
                std::size_t next = 0;
                for (const auto &part : kBodyParts)
                    for (const Bone bone : part.bones)
                        bones[next++] = bone;
                return bones;
            }();
            return order;
        }

        enum class MarkerPlace
        {
            Suffix,
            Inside,
            Prefix,
        };

        struct SideMarker
        {
            std::string_view left;
            std::string_view right;
            MarkerPlace place;
        };

        // Tried in order; the first marker found decides.
        constexpr std::array<SideMarker, 18> kSideMarkers = {{
            {".L", ".R", MarkerPlace::Suffix},
            {".l", ".r", MarkerPlace::Suffix},
            {"_L", "_R", MarkerPlace::Suffix},
            {"_l", "_r", MarkerPlace::Suffix},
            {"-L", "-R", MarkerPlace::Suffix},
            {" L", " R", MarkerPlace::Suffix},
            {"Left", "Right", MarkerPlace::Inside},
            {"left", "right", MarkerPlace::Inside},
            {"LEFT", "RIGHT", MarkerPlace::Inside},
            {" L ", " R ", MarkerPlace::Inside},
            {"_L_", "_R_", MarkerPlace::Inside},
            {"_l_", "_r_", MarkerPlace::Inside},
            {".L.", ".R.", MarkerPlace::Inside},
            {"L_", "R_", MarkerPlace::Prefix},
            {"l_", "r_", MarkerPlace::Prefix},
            {"L.", "R.", MarkerPlace::Prefix},
            {"L ", "R ", MarkerPlace::Prefix},
            {"l.", "r.", MarkerPlace::Prefix},
        }};

        // Replaces `from` with `to` where `place` allows it; false when `from` is not there.
        bool SwapMarker(std::string &name, std::size_t baseStart, std::string_view from, std::string_view to, MarkerPlace place)
        {
            std::size_t at = std::string::npos;
            switch (place)
            {
            case MarkerPlace::Suffix:
                if (name.size() >= baseStart + from.size() + 1 && name.compare(name.size() - from.size(), from.size(), from) == 0)
                    at = name.size() - from.size();
                break;
            case MarkerPlace::Inside:
                at = name.find(from, baseStart);
                break;
            case MarkerPlace::Prefix:
                if (name.size() > baseStart + from.size() && name.compare(baseStart, from.size(), from) == 0)
                    at = baseStart;
                break;
            }
            if (at == std::string::npos)
                return false;
            name.replace(at, from.size(), to);
            return true;
        }
    }

    std::span<const HumanoidBodyPartBones> GetHumanoidBodyParts() { return kBodyParts; }

    HumanoidBodyPart GetHumanoidBodyPart(Bone bone)
    {
        for (const auto &part : kBodyParts)
            for (const Bone member : part.bones)
                if (member == bone)
                    return part.part;
        return HumanoidBodyPart::Body;
    }

    Bone MirrorHumanoidBone(Bone bone)
    {
        const std::array<std::pair<std::span<const Bone>, std::span<const Bone>>, 2> sides = {{
            {kLeftArmBones, kRightArmBones},
            {kLeftLegBones, kRightLegBones},
        }};
        for (const auto &[left, right] : sides)
            for (std::size_t index = 0; index < left.size(); ++index)
            {
                if (left[index] == bone)
                    return right[index];
                if (right[index] == bone)
                    return left[index];
            }
        return bone;
    }

    Bone FindHumanoidBoneForJoint(const render::Skeleton &skeleton, int joint)
    {
        if (!render::IsValidSkeletonJoint(skeleton, joint))
            return Bone::Count;
        for (const auto &mapping : skeleton.humanoidBoneMappings)
            if (mapping.targetJointIndex == joint && static_cast<std::size_t>(mapping.bone) < render::kHumanoidBoneCount)
                return mapping.bone;
        return Bone::Count;
    }

    Bone AssignHumanoidJoint(render::Skeleton &skeleton, Bone bone, int joint)
    {
        render::EnsureHumanoidMappings(skeleton);
        if (!render::IsValidSkeletonJoint(skeleton, joint))
            joint = -1;
        Bone moved = Bone::Count;
        for (auto &mapping : skeleton.humanoidBoneMappings)
        {
            if (mapping.bone == bone)
                mapping.targetJointIndex = joint;
            else if (joint >= 0 && mapping.targetJointIndex == joint)
            {
                mapping.targetJointIndex = -1;
                moved = mapping.bone;
            }
        }
        return moved;
    }

    Bone NextUnassignedHumanoidBone(const render::Skeleton &skeleton, Bone bone)
    {
        const auto &order = ListOrder();
        std::size_t start = 0;
        for (std::size_t index = 0; index < order.size(); ++index)
            if (order[index] == bone)
                start = index + 1;
        for (std::size_t step = 0; step < order.size(); ++step)
        {
            const Bone candidate = order[(start + step) % order.size()];
            if (candidate != bone && render::GetHumanoidTargetJoint(skeleton, candidate) < 0)
                return candidate;
        }
        return Bone::Count;
    }

    std::optional<std::string> MirrorJointName(std::string_view name)
    {
        // Namespaces ("mixamorig:", "Armature|") are not part of the side marker.
        const auto separator = name.find_last_of(":|");
        const std::size_t baseStart = separator == std::string_view::npos ? 0 : separator + 1;
        for (const auto &marker : kSideMarkers)
        {
            std::string mirrored(name);
            if (SwapMarker(mirrored, baseStart, marker.left, marker.right, marker.place) ||
                SwapMarker(mirrored, baseStart, marker.right, marker.left, marker.place))
                return mirrored;
        }
        // DAZ style: a lower-case side letter before a capital ("lShldr", "rForeArm").
        if (name.size() > baseStart + 1 && (name[baseStart] == 'l' || name[baseStart] == 'r') &&
            std::isupper(static_cast<unsigned char>(name[baseStart + 1])))
        {
            std::string mirrored(name);
            mirrored[baseStart] = name[baseStart] == 'l' ? 'r' : 'l';
            return mirrored;
        }
        return std::nullopt;
    }

    int FindMirroredJoint(const render::Skeleton &skeleton, int joint)
    {
        if (!render::IsValidSkeletonJoint(skeleton, joint))
            return -1;
        const auto mirrored = MirrorJointName(skeleton.joints[static_cast<std::size_t>(joint)].name);
        if (!mirrored)
            return -1;
        for (std::size_t index = 0; index < skeleton.joints.size(); ++index)
            if (static_cast<int>(index) != joint && skeleton.joints[index].name == *mirrored)
                return static_cast<int>(index);
        return -1;
    }

    int MirrorHumanoidRig(render::Skeleton &skeleton)
    {
        render::EnsureHumanoidMappings(skeleton);
        int filled = 0;
        for (const Bone bone : ListOrder())
        {
            const Bone other = MirrorHumanoidBone(bone);
            if (other == bone || render::GetHumanoidTargetJoint(skeleton, bone) >= 0)
                continue;
            const int joint = FindMirroredJoint(skeleton, render::GetHumanoidTargetJoint(skeleton, other));
            if (joint < 0 || FindHumanoidBoneForJoint(skeleton, joint) != Bone::Count)
                continue;
            AssignHumanoidJoint(skeleton, bone, joint);
            ++filled;
        }
        return filled;
    }
}
