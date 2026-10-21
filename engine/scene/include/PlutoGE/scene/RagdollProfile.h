#pragma once

#include "PlutoGE/render/HumanoidRig.h"
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <optional>
#include <span>
#include <vector>

namespace PlutoGE::render
{
    struct Skeleton;
}

namespace PlutoGE::scene
{
    class RagdollTuningComponent;

    // The way a joint bends (its flexion), in the character's frame: a knee
    // bends its shin backward, an elbow its forearm forward.
    enum class RagdollFlexDirection : std::uint8_t { Forward, Backward, Up, Down };

    // A joint's range in degrees, relative to the bind pose: twist about the
    // bone, swing out of the flexion plane (symmetric) and flexion within it.
    struct RagdollJointLimits
    {
        float twist = 20.0f;
        float swing = 30.0f;
        float flexMin = -30.0f;
        float flexMax = 30.0f;
    };

    struct RagdollJointSettings
    {
        RagdollJointLimits limits;
        RagdollFlexDirection flexDirection = RagdollFlexDirection::Forward;
        // Share of the body mass carried by the bone below this joint.
        float massFraction = 0.02f;
        // Muscle torque at full tone, as a multiple of the torque that holds
        // everything beyond the joint out level against gravity.
        float strength = 0.6f;
        // Collision radius (m); zero sizes the capsule from the bone's length.
        float radius = 0.0f;
        // Share of the tone's initial brace this joint gets: the legs give way
        // at once (the body falls) while the trunk and arms brace.
        float brace = 1.0f;
    };

    // Muscle tone over a ragdoll's life: held at `initial` (the body braces),
    // then eased down to `sustained` (it can no longer hold itself up, but its
    // joints still resist).
    struct RagdollMuscleTone
    {
        float initial = 1.0f;
        float sustained = 0.3f;
        float holdSeconds = 0.15f;
        float decaySeconds = 1.6f;

        [[nodiscard]] float At(float seconds) const;
    };

    // How a humanoid ragdoll's body behaves. Anatomical data per humanoid bone
    // plus whole-body scales; RagdollTuningComponent overrides the scales.
    struct RagdollProfile
    {
        float bodyMass = 75.0f;
        // Multiplies every joint's muscle strength.
        float strength = 1.0f;
        // Rate (1/s) at which joint damping slows a limb's spin relative to its parent.
        float damping = 8.0f;
        // Static joint friction, as a fraction of the joint's hold torque.
        float friction = 0.05f;
        // Speed (rad/s) at which muscles pull a joint back toward its pose.
        float muscleSpeed = 4.0f;
        RagdollMuscleTone tone;
        // Off: joints keep the generic cone limits around the activation pose.
        bool anatomicalLimits = true;
        std::array<RagdollJointSettings, render::kHumanoidBoneCount> bones{};
        RagdollJointSettings other;

        static RagdollProfile Humanoid();
        [[nodiscard]] const RagdollJointSettings &For(std::optional<render::HumanoidBone> bone) const;
    };

    // The default humanoid profile with any tuning component's overrides.
    RagdollProfile ResolveRagdollProfile(const RagdollTuningComponent *tuning);
    // Copies a tuning component's whole-body values onto a profile (no-op without one).
    void ApplyRagdollTuning(RagdollProfile &profile, const RagdollTuningComponent *tuning);

    // Each joint's humanoid bone: the skeleton's explicit mapping first, then
    // the common rig names.
    std::vector<std::optional<render::HumanoidBone>> ClassifyRagdollJoints(const render::Skeleton &skeleton);

    // The character's up and forward in bind pose, from the hips, head, legs
    // or shoulders, and feet (toes point forward). Falls back to +Y and +Z.
    struct RagdollBodyAxes
    {
        glm::vec3 up{0.0f, 1.0f, 0.0f};
        glm::vec3 forward{0.0f, 0.0f, 1.0f};

        [[nodiscard]] glm::vec3 Direction(RagdollFlexDirection direction) const;
    };
    RagdollBodyAxes EstimateRagdollBodyAxes(std::span<const std::optional<render::HumanoidBone>> bones,
                                            std::span<const glm::vec3> bindPositions);

    // A joint's anatomical frame (columns: X along the bone, Y its flexion
    // direction, Z the flexion axis), so that positive rotation about Z bends
    // the bone toward its flexion. A flexion parallel to the bone falls back
    // to any perpendicular.
    glm::mat3 RagdollJointBasis(const glm::vec3 &boneDirection, const glm::vec3 &flexDirection);
}
