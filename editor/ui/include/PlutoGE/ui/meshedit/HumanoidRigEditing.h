#pragma once

#include "PlutoGE/render/HumanoidRig.h"
#include "PlutoGE/render/Mesh.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>

// Editing operations behind the rig editor's humanoid map. No ImGui here, so it can be tested.
namespace PlutoGE::ui::meshedit
{
    enum class HumanoidBodyPart
    {
        Body,
        LeftArm,
        RightArm,
        LeftLeg,
        RightLeg,
    };

    struct HumanoidBodyPartBones
    {
        HumanoidBodyPart part;
        const char *label;
        std::span<const render::HumanoidBone> bones; // from the body outward
    };

    // Every humanoid bone, grouped by body part in the order the editor lists them.
    [[nodiscard]] std::span<const HumanoidBodyPartBones> GetHumanoidBodyParts();
    [[nodiscard]] HumanoidBodyPart GetHumanoidBodyPart(render::HumanoidBone bone);
    // Left Hand <-> Right Hand; body bones map to themselves.
    [[nodiscard]] render::HumanoidBone MirrorHumanoidBone(render::HumanoidBone bone);

    // The slot whose joint is `joint`, Count when none.
    [[nodiscard]] render::HumanoidBone FindHumanoidBoneForJoint(const render::Skeleton &skeleton, int joint);

    // Puts `joint` (-1 to clear) in `bone`'s slot, creating the map if needed. A joint fills one slot, so a slot
    // that held it is cleared; returns that slot, Count when none.
    render::HumanoidBone AssignHumanoidJoint(render::Skeleton &skeleton, render::HumanoidBone bone, int joint);

    // The first slot after `bone`, in list order and wrapping round, that has no joint. Count when all have one.
    [[nodiscard]] render::HumanoidBone NextUnassignedHumanoidBone(const render::Skeleton &skeleton, render::HumanoidBone bone);

    // The name with its side swapped: "hand.L" -> "hand.R", "mixamorig:LeftArm" -> "mixamorig:RightArm",
    // "Bip01 L Thigh" -> "Bip01 R Thigh", "lShldr" -> "rShldr". Nullopt when the name has no side marker.
    [[nodiscard]] std::optional<std::string> MirrorJointName(std::string_view name);
    // The joint named as `joint`'s other side, -1 when none.
    [[nodiscard]] int FindMirroredJoint(const render::Skeleton &skeleton, int joint);
    // Fills each empty slot on one side from its mirror slot's joint on the other side. Returns the slots filled.
    int MirrorHumanoidRig(render::Skeleton &skeleton);
}
