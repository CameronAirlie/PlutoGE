#pragma once
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/SkeletonJointLookup.h"
#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace PlutoGE::scene
{
    // A skinned mesh posed by another skinned mesh's skeleton: clothing, armour or hair authored on a character's rig
    // and exported on its own. Each joint of the follower takes the pose of the source joint it matches, so the
    // follower's skeleton may hold a subset of the source's joints, in any order, with its own bind pose, as long as it
    // was authored in the same space as the source (modelled over the body, exported from the same armature).
    //
    // A follower joint matches by exact name, then by the humanoid bone its own mapping gives it, then by humanoid
    // aliases (FindSkeletonJoint: "Head", "mixamorig:Spine", ...). A joint with no match (a skirt or a coat-tail bone)
    // moves rigidly with its nearest matched ancestor; one with no matched ancestor stays at its bind pose.
    //
    // MeshComponent uses it for meshes that follow their parent's skeleton (SetFollowParentSkeleton).

    // How a follower joint found its source joint.
    enum class JointMatchKind : std::uint8_t
    {
        None,
        Name,           // The same name: the same armature.
        HumanoidMapping, // The follower's humanoid mapping names a bone the source maps too.
        Alias,          // A humanoid alias of the name ("Head", "mixamorig:Head"): probably another rig.
    };

    struct JointMatch
    {
        int sourceJoint = -1;
        JointMatchKind kind = JointMatchKind::None;
    };

    struct SkinPoseFollowMap
    {
        // Per follower joint: the follower joint whose match poses it (itself when matched, a matched ancestor when
        // not), or -1 when nothing poses it.
        std::vector<int> anchor;
        // Per follower joint: the source joint it matches, or -1.
        std::vector<int> sourceJoint;
        // Per follower joint: how it matched.
        std::vector<JointMatchKind> matchKind;
        // Per matched follower joint: source bind (mesh space) times the follower's inverse bind. Identity when both
        // were bound in the same pose.
        std::vector<glm::mat4> bindOffset;
        std::size_t matchedJoints = 0;
    };

    [[nodiscard]] inline JointMatch MatchFollowerJoint(const render::Skeleton &source, const render::Skeleton &follower,
                                                       std::size_t followerJoint)
    {
        const auto &name = follower.joints[followerJoint].name;
        for (std::size_t joint = 0; joint < source.joints.size(); ++joint)
            if (source.joints[joint].name == name)
                return {static_cast<int>(joint), JointMatchKind::Name};
        // The follower's own humanoid mapping names the bone; the source's mapping says which of its joints that is.
        for (const auto &mapping : follower.humanoidBoneMappings)
        {
            if (mapping.targetJointIndex != static_cast<int>(followerJoint))
                continue;
            for (const auto &sourceMapping : source.humanoidBoneMappings)
                if (sourceMapping.bone == mapping.bone && sourceMapping.targetJointIndex >= 0 &&
                    sourceMapping.targetJointIndex < static_cast<int>(source.joints.size()))
                    return {sourceMapping.targetJointIndex, JointMatchKind::HumanoidMapping};
        }
        const int alias = render::FindSkeletonJoint(source, name);
        return alias >= 0 ? JointMatch{alias, JointMatchKind::Alias} : JointMatch{};
    }

    [[nodiscard]] inline SkinPoseFollowMap BuildSkinPoseFollowMap(const render::Skeleton &source,
                                                                  const render::Skeleton &follower)
    {
        const auto count = follower.joints.size();
        SkinPoseFollowMap map;
        map.anchor.assign(count, -1);
        map.sourceJoint.assign(count, -1);
        map.matchKind.assign(count, JointMatchKind::None);
        map.bindOffset.assign(count, glm::mat4(1.0f));
        for (std::size_t joint = 0; joint < count; ++joint)
        {
            const auto match = MatchFollowerJoint(source, follower, joint);
            if (match.sourceJoint < 0)
                continue;
            map.sourceJoint[joint] = match.sourceJoint;
            map.matchKind[joint] = match.kind;
            map.bindOffset[joint] = glm::inverse(source.joints[static_cast<std::size_t>(match.sourceJoint)].inverseBindMatrix) *
                                    follower.joints[joint].inverseBindMatrix;
            ++map.matchedJoints;
        }
        for (std::size_t joint = 0; joint < count; ++joint)
        {
            // The nearest matched joint on the way to the root, itself included.
            int current = static_cast<int>(joint);
            for (std::size_t steps = 0; current >= 0 && static_cast<std::size_t>(current) < count && steps <= count; ++steps)
            {
                if (map.sourceJoint[static_cast<std::size_t>(current)] >= 0)
                {
                    map.anchor[joint] = current;
                    break;
                }
                current = follower.joints[static_cast<std::size_t>(current)].parentJointIndex;
            }
        }
        return map;
    }

    // Writes the follower's skinning palette from the source's (posed joint transform times inverse bind, one per
    // source joint). A joint posed rigidly by an ancestor shares the ancestor's matrix: the palette maps bind-space
    // vertices, so equal matrices move both joints' geometry as one piece.
    inline void ApplySkinPoseFollow(const SkinPoseFollowMap &map, std::span<const glm::mat4> sourcePalette,
                                    std::vector<glm::mat4> &palette)
    {
        palette.assign(map.anchor.size(), glm::mat4(1.0f));
        for (std::size_t joint = 0; joint < map.anchor.size(); ++joint)
        {
            const int anchor = map.anchor[joint];
            if (anchor < 0)
                continue;
            const int source = map.sourceJoint[static_cast<std::size_t>(anchor)];
            if (source < 0 || static_cast<std::size_t>(source) >= sourcePalette.size())
                continue;
            palette[joint] = sourcePalette[static_cast<std::size_t>(source)] * map.bindOffset[static_cast<std::size_t>(anchor)];
        }
    }

    // ---- fit -------------------------------------------------------------------------------------------------------
    //
    // Whether a follower was made for its source's body. Each follower joint takes its source joint's pose, so a
    // garment made for a body of other proportions (taller, broader) lands off it: its shoulders and hood sit too high or
    // too low and skin shows through. Rest poses may differ (an A-posed garment on a T-posed body is fine), so fit is
    // judged on proportions: the length of each bone between matched joints, and where the topmost matched joint (the
    // hips) stands, compared between the two binds.

    enum class SkinFitQuality : std::uint8_t
    {
        Fits,       // Proportions agree within SkinFitTolerance: made for this body.
        Loose,      // Within SkinFitLooseTolerance: wearable, may clip or float a little.
        OtherBody,  // Made for a body of other proportions.
        Unmatched,  // No joint matched: it cannot follow this body at all.
    };

    inline constexpr float SkinFitTolerance = 0.01f;      // metres
    inline constexpr float SkinFitLooseTolerance = 0.03f; // metres

    struct SkinFitReport
    {
        std::size_t joints = 0;
        std::size_t matchedByName = 0;
        std::size_t matchedByMapping = 0;
        std::size_t matchedByAlias = 0;
        // The largest difference in bone length or root height between the binds, and the follower joint it is at.
        float largestProportionError = 0.0f;
        std::string worstJoint;
        SkinFitQuality quality = SkinFitQuality::Unmatched;

        [[nodiscard]] std::size_t Matched() const { return matchedByName + matchedByMapping + matchedByAlias; }
    };

    [[nodiscard]] inline SkinFitQuality ClassifySkinFit(std::size_t matched, float proportionError)
    {
        if (matched == 0)
            return SkinFitQuality::Unmatched;
        if (proportionError <= SkinFitTolerance)
            return SkinFitQuality::Fits;
        return proportionError <= SkinFitLooseTolerance ? SkinFitQuality::Loose : SkinFitQuality::OtherBody;
    }

    [[nodiscard]] inline SkinFitReport AssessSkinFit(const render::Skeleton &source, const render::Skeleton &follower,
                                                     const SkinPoseFollowMap &map)
    {
        SkinFitReport report;
        report.joints = follower.joints.size();
        const auto bindPosition = [](const render::SkeletonJoint &joint)
        { return glm::vec3(glm::inverse(joint.inverseBindMatrix)[3]); };
        const auto consider = [&](float error, std::size_t joint)
        {
            if (error <= report.largestProportionError)
                return;
            report.largestProportionError = error;
            report.worstJoint = follower.joints[joint].name;
        };
        for (std::size_t joint = 0; joint < follower.joints.size() && joint < map.sourceJoint.size(); ++joint)
        {
            const int sourceJoint = map.sourceJoint[joint];
            if (sourceJoint < 0)
                continue;
            switch (map.matchKind[joint])
            {
            case JointMatchKind::Name: ++report.matchedByName; break;
            case JointMatchKind::HumanoidMapping: ++report.matchedByMapping; break;
            case JointMatchKind::Alias: ++report.matchedByAlias; break;
            case JointMatchKind::None: break;
            }
            const glm::vec3 followerPosition = bindPosition(follower.joints[joint]);
            const glm::vec3 sourcePosition = bindPosition(source.joints[static_cast<std::size_t>(sourceJoint)]);
            // The bone from the nearest matched ancestor; a joint with none is a root, compared by its height.
            const int parent = follower.joints[joint].parentJointIndex;
            const int ancestor = parent >= 0 && static_cast<std::size_t>(parent) < map.anchor.size() ? map.anchor[static_cast<std::size_t>(parent)] : -1;
            if (ancestor < 0)
            {
                consider(std::abs(followerPosition.y - sourcePosition.y), joint);
                continue;
            }
            const auto sourceAncestor = static_cast<std::size_t>(map.sourceJoint[static_cast<std::size_t>(ancestor)]);
            const float followerLength = glm::length(followerPosition - bindPosition(follower.joints[static_cast<std::size_t>(ancestor)]));
            const float sourceLength = glm::length(sourcePosition - bindPosition(source.joints[sourceAncestor]));
            consider(std::abs(followerLength - sourceLength), joint);
        }
        report.quality = ClassifySkinFit(report.Matched(), report.largestProportionError);
        return report;
    }
}
