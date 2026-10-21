#pragma once

#include "PlutoGE/scene/RagdollProfile.h"
#include <BulletDynamics/ConstraintSolver/btGeneric6DofSpring2Constraint.h>

// Bullet side of humanoid ragdoll joints (see RagdollProfile): anatomical
// limits and the muscle that holds each joint. Private to the scene library.
namespace PlutoGE::scene::detail
{
    // One joint's muscle, measured when the ragdoll is built.
    struct RagdollMuscle
    {
        // Torque (N·m) that holds everything beyond the joint level against gravity.
        float holdTorque = 0.0f;
        // The joint's strength and brace from the profile (scaled live by the profile's strength).
        float strength = 0.0f;
        float brace = 1.0f;
        // Inertia (kg·m²) of everything beyond the joint about it, which scales its damping.
        float distalInertia = 0.0f;
        // Joint angles (rad, the constraint's XYZ) the muscle holds: the pose the ragdoll started in.
        btVector3 pose{0.0f, 0.0f, 0.0f};
    };

    // Sets anatomical limits (widened to contain the starting pose, so nothing
    // snaps on activation) and returns the muscle holding that pose. The
    // constraint's frames must already be anatomical: X along the bone, Z the
    // flexion axis, zero at the bind pose.
    RagdollMuscle ConfigureAnatomicalJoint(btGeneric6DofSpring2Constraint &joint, const RagdollJointSettings &settings,
                                           float holdTorque, float distalInertia);

    // Applies the muscle at the body's `tone` (0 = limp; the joint gets its
    // brace share of the tone above the sustained level): a capped servo toward its pose
    // (bounded torque, solved implicitly, so it cannot explode), joint friction
    // and damping of the limb's spin relative to its parent.
    void DriveRagdollMuscle(btGeneric6DofSpring2Constraint &joint, const RagdollMuscle &muscle,
                            const RagdollProfile &profile, float tone);
}
