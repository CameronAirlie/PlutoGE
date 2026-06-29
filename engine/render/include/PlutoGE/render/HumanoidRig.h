#pragma once

#include <array>
#include <cctype>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

#include <glm/glm.hpp>

namespace PlutoGE::render
{
    enum class HumanoidBone : std::uint8_t
    {
        Hips,
        Spine,
        Chest,
        UpperChest,
        Neck,
        Head,
        LeftShoulder,
        LeftUpperArm,
        LeftLowerArm,
        LeftHand,
        RightShoulder,
        RightUpperArm,
        RightLowerArm,
        RightHand,
        LeftUpperLeg,
        LeftLowerLeg,
        LeftFoot,
        LeftToes,
        RightUpperLeg,
        RightLowerLeg,
        RightFoot,
        RightToes,
        Count,
    };

    constexpr std::size_t kHumanoidBoneCount = static_cast<std::size_t>(HumanoidBone::Count);

    struct HumanoidBoneMapping
    {
        HumanoidBone bone = HumanoidBone::Hips;
        // Optional exact/namespace-insensitive source channel name. When empty,
        // common humanoid aliases are recognized automatically.
        std::string sourceBoneName;
        int targetJointIndex = -1;
        glm::vec3 rotationOffsetDegrees{0.0f};
        bool copyTranslation = false;
        float translationScale = 1.0f;
    };

    inline constexpr std::array<const char *, kHumanoidBoneCount> kHumanoidBoneNames = {
        "Hips", "Spine", "Chest", "Upper Chest", "Neck", "Head",
        "Left Shoulder", "Left Upper Arm", "Left Lower Arm", "Left Hand",
        "Right Shoulder", "Right Upper Arm", "Right Lower Arm", "Right Hand",
        "Left Upper Leg", "Left Lower Leg", "Left Foot", "Left Toes",
        "Right Upper Leg", "Right Lower Leg", "Right Foot", "Right Toes"};

    inline const char *HumanoidBoneName(HumanoidBone bone)
    {
        const auto index = static_cast<std::size_t>(bone);
        return index < kHumanoidBoneNames.size() ? kHumanoidBoneNames[index] : "Unknown";
    }

    inline std::string NormalizeHumanoidBoneName(std::string_view name)
    {
        const auto separator = name.find_last_of(":|/");
        if (separator != std::string_view::npos)
        {
            name.remove_prefix(separator + 1);
        }

        std::string normalized;
        normalized.reserve(name.size());
        for (const unsigned char character : name)
        {
            if (std::isalnum(character))
            {
                normalized.push_back(static_cast<char>(std::tolower(character)));
            }
        }
        return normalized;
    }

    inline bool IsOneOf(std::string_view value, std::initializer_list<std::string_view> candidates)
    {
        for (const auto candidate : candidates)
        {
            if (value == candidate)
                return true;
        }
        return false;
    }

    inline std::optional<HumanoidBone> GuessHumanoidBone(std::string_view jointName)
    {
        const std::string name = NormalizeHumanoidBoneName(jointName);
        if (IsOneOf(name, {"hips", "pelvis", "bip01pelvis"})) return HumanoidBone::Hips;
        if (IsOneOf(name, {"spine", "spine01", "bip01spine"})) return HumanoidBone::Spine;
        if (IsOneOf(name, {"chest", "spine1", "spine02", "bip01spine1"})) return HumanoidBone::Chest;
        if (IsOneOf(name, {"upperchest", "spine2", "spine03", "bip01spine2"})) return HumanoidBone::UpperChest;
        if (IsOneOf(name, {"neck", "neck1", "bip01neck"})) return HumanoidBone::Neck;
        if (IsOneOf(name, {"head", "bip01head"})) return HumanoidBone::Head;

        if (IsOneOf(name, {"leftshoulder", "claviclel", "lclavicle", "bip01lclavicle"})) return HumanoidBone::LeftShoulder;
        if (IsOneOf(name, {"leftarm", "leftupperarm", "upperarml", "lupperarm", "bip01lupperarm"})) return HumanoidBone::LeftUpperArm;
        if (IsOneOf(name, {"leftforearm", "leftlowerarm", "lowerarml", "lforearm", "bip01lforearm"})) return HumanoidBone::LeftLowerArm;
        if (IsOneOf(name, {"lefthand", "handl", "lhand", "bip01lhand"})) return HumanoidBone::LeftHand;
        if (IsOneOf(name, {"rightshoulder", "clavicler", "rclavicle", "bip01rclavicle"})) return HumanoidBone::RightShoulder;
        if (IsOneOf(name, {"rightarm", "rightupperarm", "upperarmr", "rupperarm", "bip01rupperarm"})) return HumanoidBone::RightUpperArm;
        if (IsOneOf(name, {"rightforearm", "rightlowerarm", "lowerarmr", "rforearm", "bip01rforearm"})) return HumanoidBone::RightLowerArm;
        if (IsOneOf(name, {"righthand", "handr", "rhand", "bip01rhand"})) return HumanoidBone::RightHand;

        if (IsOneOf(name, {"leftupleg", "leftupperleg", "thighl", "lthigh", "bip01lthigh"})) return HumanoidBone::LeftUpperLeg;
        if (IsOneOf(name, {"leftleg", "leftlowerleg", "calfl", "lcalf", "leftshin", "bip01lcalf"})) return HumanoidBone::LeftLowerLeg;
        if (IsOneOf(name, {"leftfoot", "footl", "lfoot", "bip01lfoot"})) return HumanoidBone::LeftFoot;
        if (IsOneOf(name, {"lefttoebase", "lefttoes", "balll", "ltoe", "bip01ltoe0"})) return HumanoidBone::LeftToes;
        if (IsOneOf(name, {"rightupleg", "rightupperleg", "thighr", "rthigh", "bip01rthigh"})) return HumanoidBone::RightUpperLeg;
        if (IsOneOf(name, {"rightleg", "rightlowerleg", "calfr", "rcalf", "rightshin", "bip01rcalf"})) return HumanoidBone::RightLowerLeg;
        if (IsOneOf(name, {"rightfoot", "footr", "rfoot", "bip01rfoot"})) return HumanoidBone::RightFoot;
        if (IsOneOf(name, {"righttoebase", "righttoes", "ballr", "rtoe", "bip01rtoe0"})) return HumanoidBone::RightToes;
        return std::nullopt;
    }
}
