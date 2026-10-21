#include "RagdollJoints.h"

#include <algorithm>
#include <glm/glm.hpp>

namespace PlutoGE::scene::detail
{
    namespace
    {
        // Spring2's middle (Y) axis must stay inside +-90 degrees.
        constexpr float kDegrees = 3.14159265f / 180.0f;
        constexpr float kMaxSwingRadians = 85.0f * kDegrees;
        constexpr float kMaxAngleRadians = 175.0f * kDegrees;
        constexpr float kPoseMarginRadians = 2.0f * kDegrees;
        constexpr float kLimitErrorReduction = 0.35f;
        // Every joint resists a little, however light the limb beyond it.
        constexpr float kMinimumFrictionTorque = 0.3f;
    }

    RagdollMuscle ConfigureAnatomicalJoint(btGeneric6DofSpring2Constraint &joint, const RagdollJointSettings &settings,
                                           float holdTorque, float distalInertia)
    {
        joint.calculateTransforms();
        RagdollMuscle muscle{.holdTorque = holdTorque, .strength = settings.strength, .brace = settings.brace,
                             .distalInertia = distalInertia};
        // Spring2 reports angles as the parent frame's rotation relative to the
        // child's (matrixToEulerXYZ reads the transpose), the negative of the
        // anatomical angle: flexion of the child is a negative Z angle.
        const auto &limits = settings.limits;
        const btVector3 lower(-glm::radians(limits.twist), -glm::radians(limits.swing), -glm::radians(limits.flexMax));
        const btVector3 upper(glm::radians(limits.twist), glm::radians(limits.swing), -glm::radians(limits.flexMin));
        btVector3 low = lower, high = upper;
        for (int axis = 0; axis < 3; ++axis)
        {
            const float cap = axis == 1 ? kMaxSwingRadians : kMaxAngleRadians;
            const float pose = std::clamp(static_cast<float>(joint.getAngle(axis)), -cap, cap);
            low[axis] = std::clamp(std::min(static_cast<float>(low[axis]), pose - kPoseMarginRadians), -cap, cap);
            high[axis] = std::clamp(std::max(static_cast<float>(high[axis]), pose + kPoseMarginRadians), -cap, cap);
            muscle.pose[axis] = pose;
        }
        joint.setAngularLowerLimit(low);
        joint.setAngularUpperLimit(high);
        for (int axis = 3; axis < 6; ++axis)
        {
            joint.setBounce(axis, 0.0f);
            joint.setParam(BT_CONSTRAINT_STOP_ERP, kLimitErrorReduction, axis);
            joint.setParam(BT_CONSTRAINT_STOP_CFM, 0.0f, axis);
            joint.enableMotor(axis, true);
            // A damping-only spring: no rest pose of its own, it slows relative spin.
            joint.enableSpring(axis, true);
            joint.setStiffness(axis, 0.0f, true);
        }
        return muscle;
    }

    void DriveRagdollMuscle(btGeneric6DofSpring2Constraint &joint, const RagdollMuscle &muscle,
                            const RagdollProfile &profile, float tone)
    {
        const float sustained = profile.tone.sustained;
        const float jointTone = std::max(sustained + (tone - sustained) * muscle.brace, 0.0f);
        const float torque = jointTone * profile.strength * muscle.strength * muscle.holdTorque +
                             profile.friction * muscle.holdTorque + kMinimumFrictionTorque;
        const float damping = profile.damping * muscle.distalInertia;
        for (int axis = 3; axis < 6; ++axis)
        {
            joint.enableMotor(axis, true);
            joint.setServo(axis, true);
            joint.setServoTarget(axis, muscle.pose[axis - 3]);
            joint.setTargetVelocity(axis, profile.muscleSpeed);
            joint.setMaxMotorForce(axis, torque);
            joint.setDamping(axis, damping, true);
        }
    }
}
