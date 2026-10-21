#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/RagdollProfile.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/RagdollTuningComponent.h"
#include <cmath>
#include <cstdlib>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>
#include <string>

// Humanoid ragdoll muscles and anatomical joints (RagdollProfile, RagdollJoints):
// the profile's helpers; then a T-pose humanoid dropped on the ground, whose
// knees and elbows must never bend backward, which must hold its shape better
// than the same body made limp, and which must settle rather than jitter.
namespace
{
    using namespace PlutoGE;

    void Require(bool value, const std::string &message) { if (!value) throw std::runtime_error(message); }
    bool Near(const glm::vec3 &a, const glm::vec3 &b, float tolerance) { return glm::length(a - b) <= tolerance; }

    // Facing +Z with Y up: the character's left is +X.
    struct Bone { const char *name; int parent; glm::vec3 position; };
    constexpr Bone kRig[] = {
        {"Hips", -1, {0, 1.0f, 0}},          {"Spine", 0, {0, 1.1f, 0}},           {"Spine1", 1, {0, 1.25f, 0}},
        {"Spine2", 2, {0, 1.4f, 0}},          {"Neck", 3, {0, 1.55f, 0}},           {"Head", 4, {0, 1.65f, 0}},
        {"LeftShoulder", 3, {0.05f, 1.5f, 0}}, {"LeftArm", 6, {0.18f, 1.5f, 0}},    {"LeftForeArm", 7, {0.46f, 1.5f, 0}},
        {"LeftHand", 8, {0.72f, 1.5f, 0}},     {"RightShoulder", 3, {-0.05f, 1.5f, 0}}, {"RightArm", 10, {-0.18f, 1.5f, 0}},
        {"RightForeArm", 11, {-0.46f, 1.5f, 0}}, {"RightHand", 12, {-0.72f, 1.5f, 0}}, {"LeftUpLeg", 0, {0.1f, 0.95f, 0}},
        {"LeftLeg", 14, {0.1f, 0.52f, 0}},     {"LeftFoot", 15, {0.1f, 0.1f, 0}},    {"LeftToeBase", 16, {0.1f, 0.02f, 0.14f}},
        {"RightUpLeg", 0, {-0.1f, 0.95f, 0}},  {"RightLeg", 18, {-0.1f, 0.52f, 0}},  {"RightFoot", 19, {-0.1f, 0.1f, 0}},
        {"RightToeBase", 20, {-0.1f, 0.02f, 0.14f}},
    };
    enum : int { Hips = 0, Head = 5, LeftArm = 7, LeftForeArm = 8, LeftHand = 9, LeftUpLeg = 14, LeftLeg = 15, LeftFoot = 16,
                 RightUpLeg = 18, RightLeg = 19, RightFoot = 20 };
    constexpr int kJointCount = static_cast<int>(sizeof(kRig) / sizeof(kRig[0]));

    render::MeshConfig HumanoidConfig()
    {
        render::MeshConfig config;
        for (const auto &bone : kRig)
        {
            render::SkeletonJoint joint{.name = bone.name, .parentJointIndex = bone.parent};
            const glm::vec3 parent = bone.parent >= 0 ? kRig[bone.parent].position : glm::vec3(0.0f);
            joint.localBindTransform = glm::translate(glm::mat4(1.0f), bone.position - parent);
            joint.inverseBindMatrix = glm::translate(glm::mat4(1.0f), -bone.position);
            config.skeleton.joints.push_back(joint);
        }
        return config;
    }

    // One dropped body; `limp` removes muscles, joint damping and friction (anatomical limits stay).
    struct Body
    {
        scene::Scene world;
        scene::AnimationComponent *animation = nullptr;
        const render::Skeleton *skeleton = nullptr;

        Body(render::Mesh &mesh, bool limp)
        {
            auto *ground = world.AddEntity(std::make_unique<scene::Entity>());
            ground->SetPosition({0, -0.5f, 0});
            ground->CreateComponent<scene::ColliderComponent>(scene::ColliderComponentConfig{.size = {40, 1, 40}});
            auto *entity = world.AddEntity(std::make_unique<scene::Entity>());
            entity->SetPosition({0, 0.02f, 0}); // Toes resting on the ground.
            entity->CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{.mesh = &mesh});
            animation = entity->CreateComponent<scene::AnimationComponent>();
            if (limp)
            {
                auto *tuning = entity->CreateComponent<scene::RagdollTuningComponent>();
                tuning->SetMuscleStrength(0);
                tuning->SetInitialTone(0);
                tuning->SetSustainedTone(0);
                tuning->SetJointDamping(0);
                tuning->SetJointFriction(0);
            }
            animation->SetRagdollWeight(1);
            animation->SetRagdollEnabled(true);
            // A killing blow from the front (the game pushes along the blow).
            animation->AddRagdollImpulse({0, 0, -25});
            skeleton = &mesh.GetSkeleton();
            world.StartRuntime();
        }
        ~Body() { world.StopRuntime(); }

        void Step(int frames) { for (int i = 0; i < frames; ++i) world.Update(1.0f / 60.0f); }
        glm::mat4 Joint(int index) const
        {
            const auto &skin = animation->GetJointMatrices(*skeleton);
            Require(skin.size() == static_cast<size_t>(kJointCount), "Ragdoll pose missing");
            return animation->GetOwner()->GetWorldTransform() * skin[static_cast<size_t>(index)] *
                   glm::translate(glm::mat4(1.0f), kRig[index].position);
        }
        glm::vec3 Position(int index) const { return glm::vec3(Joint(index)[3]); }
        glm::mat3 Rotation(int index) const
        {
            glm::mat3 rotation(Joint(index));
            for (int c = 0; c < 3; ++c) rotation[c] = glm::normalize(rotation[c]);
            return rotation;
        }

        // Signed flexion (degrees) of the joint between `upper` and `lower`
        // about the hinge carried by `upper` (bind hinge `hinge`).
        float Flexion(int upper, int lower, int end, const glm::vec3 &hinge) const
        {
            const glm::vec3 a = glm::normalize(Position(lower) - Position(upper));
            const glm::vec3 b = glm::normalize(Position(end) - Position(lower));
            const glm::vec3 axis = Rotation(upper) * hinge;
            return glm::degrees(std::atan2(glm::dot(glm::cross(a, b), axis), glm::dot(a, b)));
        }

        // How far the body has bent from its starting (bind) shape: the mean
        // joint angle, each bone's rotation relative to its parent's.
        float Bend() const
        {
            float total = 0.0f;
            for (int joint = 1; joint < kJointCount; ++joint)
                total += JointAngle(joint);
            return total / static_cast<float>(kJointCount - 1);
        }
        float JointAngle(int joint) const
        {
            const glm::mat3 relative = glm::transpose(Rotation(kRig[joint].parent)) * Rotation(joint);
            const float cosine = (relative[0][0] + relative[1][1] + relative[2][2] - 1.0f) * 0.5f;
            return glm::degrees(std::acos(std::clamp(cosine, -1.0f, 1.0f)));
        }
    };
}

int main()
{
    try
    {
        // Tone: braces, then eases down to the sustained level.
        const scene::RagdollMuscleTone tone;
        Require(tone.At(0) == tone.initial && tone.At(tone.holdSeconds) == tone.initial, "Tone does not brace first");
        Require(std::abs(tone.At(tone.holdSeconds + tone.decaySeconds + 1) - tone.sustained) < 1e-5f, "Tone does not settle");
        for (float t = 0; t < 3; t += 0.05f)
            Require(tone.At(t + 0.05f) <= tone.At(t) + 1e-6f, "Tone rises while relaxing");

        // A joint frame bends its bone toward its flexion about +Z.
        const glm::mat3 basis = scene::RagdollJointBasis({0, -1, 0}, {0, 0.3f, -1});
        Require(std::abs(glm::determinant(basis) - 1) < 1e-4f && std::abs(glm::dot(basis[0], basis[1])) < 1e-5f, "Joint frame is not a rotation");
        const glm::vec3 bent = glm::mat3(glm::rotate(glm::mat4(1), glm::radians(30.0f), basis[2])) * basis[0];
        Require(glm::dot(bent, glm::vec3(0, 0, -1)) > 0.45f, "Positive flexion does not bend toward the flexion direction");

        // Tuning is scene data: it round-trips and resolves onto the profile.
        {
            scene::Scene saved;
            auto *entity = saved.AddEntity(std::make_unique<scene::Entity>());
            auto *tuning = entity->CreateComponent<scene::RagdollTuningComponent>();
            tuning->SetBodyMass(90);
            tuning->SetSustainedTone(0.1f);
            tuning->SetAnatomicalLimits(false);
            std::string text, error;
            Require(scene::SceneSerializer::SaveToString(saved, text, &error), "Save tuning: " + error);
            const auto loaded = scene::SceneSerializer::LoadFromString(text, &error);
            const auto *entityCopy = loaded ? loaded->FindEntityByID(entity->GetID()) : nullptr;
            const auto *copy = entityCopy ? entityCopy->GetComponent<scene::RagdollTuningComponent>() : nullptr;
            Require(copy != nullptr, "Tuning component lost in a scene round trip: " + error);
            const auto resolved = scene::ResolveRagdollProfile(copy);
            Require(resolved.bodyMass == 90 && std::abs(resolved.tone.sustained - 0.1f) < 1e-5f && !resolved.anatomicalLimits,
                    "Tuning did not resolve onto the profile");
        }

        render::Mesh mesh(HumanoidConfig());
        const auto bones = scene::ClassifyRagdollJoints(mesh.GetSkeleton());
        Require(std::all_of(bones.begin(), bones.end(), [](auto bone) { return bone.has_value(); }), "Humanoid bones unrecognised");
        std::vector<glm::vec3> positions;
        for (const auto &bone : kRig) positions.push_back(bone.position);
        const auto axes = scene::EstimateRagdollBodyAxes(bones, positions);
        Require(Near(axes.up, {0, 1, 0}, 0.01f) && Near(axes.forward, {0, 0, 1}, 0.01f), "Body axes from the bind pose");
        const auto profile = scene::RagdollProfile::Humanoid();
        float massTotal = 0;
        for (auto bone : bones) massTotal += profile.For(bone).massFraction;
        Require(massTotal > 0.8f && massTotal < 1.2f, "Segment masses do not describe a whole body");

        // Hinges in bind pose: the knee bends its shin backward about +X, the
        // left elbow its forearm forward about -Y.
        const glm::vec3 kneeHinge = glm::cross(glm::vec3(0, -1, 0), glm::vec3(0, 0, -1));
        const glm::vec3 elbowHinge = glm::cross(glm::vec3(1, 0, 0), glm::vec3(0, 0, 1));
        Body stiff(mesh, false), limp(mesh, true);
        const glm::vec3 start = stiff.Position(Hips);
        float worstKnee = 0, worstElbow = 0, stiffBend = 0, limpBend = 0, stiffShoulder = 0, limpShoulder = 0;
        glm::vec3 lastHead{};
        int stiffFallen = 0; // First frame the hips are below half their height.
        for (int frame = 1; frame <= 300; ++frame)
        {
            stiff.Step(1);
            limp.Step(1);
            for (auto *body : {&stiff, &limp})
            {
                worstKnee = std::min({worstKnee, body->Flexion(LeftUpLeg, LeftLeg, LeftFoot, kneeHinge),
                                      body->Flexion(RightUpLeg, RightLeg, RightFoot, kneeHinge)});
                worstElbow = std::min(worstElbow, body->Flexion(LeftArm, LeftForeArm, LeftHand, elbowHinge));
                for (int joint = 0; joint < kJointCount; ++joint)
                {
                    const glm::vec3 p = body->Position(joint);
                    Require(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && glm::length(p - start) < 4,
                            "Ragdoll exploded");
                }
            }
            if (std::getenv("RAGDOLL_TRACE") && (frame < 6 || frame % 30 == 0))
                for (auto *body : {&stiff, &limp})
                    std::cout << (body == &stiff ? "stiff" : "limp ") << " f" << frame << " knee " << body->Flexion(LeftUpLeg, LeftLeg, LeftFoot, kneeHinge)
                              << " elbow " << body->Flexion(LeftArm, LeftForeArm, LeftHand, elbowHinge) << " hips " << body->Position(Hips).y
                              << " head " << body->Position(Head).y << " hand " << body->Position(LeftHand).y << " bend " << body->Bend() << '\n';
            if (frame == 12)
            {
                stiffShoulder = stiff.JointAngle(LeftArm);
                limpShoulder = limp.JointAngle(LeftArm);
            }
            if (frame == 45) { stiffBend = stiff.Bend(); limpBend = limp.Bend(); }
            if (frame == 270) lastHead = stiff.Position(Head);
            if (!stiffFallen && stiff.Position(Hips).y < 0.5f) stiffFallen = frame;
        }
        std::cout << "knee " << worstKnee << " elbow " << worstElbow << " bend stiff " << stiffBend << " limp " << limpBend
                  << " fallen " << stiffFallen << " shoulder stiff " << stiffShoulder << " limp " << limpShoulder << " settle " << glm::length(stiff.Position(Head) - lastHead) << '\n';
        Require(worstKnee > -8.0f, "A knee bent backward (" + std::to_string(worstKnee) + " degrees)");
        Require(worstElbow > -12.0f, "An elbow bent backward (" + std::to_string(worstElbow) + " degrees)");
        Require(stiffShoulder < limpShoulder * 0.5f, "Muscles did not hold the arms up at first (shoulder " + std::to_string(stiffShoulder) +
                " degrees, limp " + std::to_string(limpShoulder) + ")");
        Require(stiffBend < limpBend * 0.75f, "The muscled body bent as much as a limp one");
        Require(glm::length(stiff.Position(Head) - lastHead) < 0.03f, "The body did not settle");
        Require(stiff.Position(Head).y < 0.6f, "The body is still standing after five seconds");
        Require(stiff.Position(Hips).y < 0.5f && stiffFallen > 0 && stiffFallen <= 90,
                "The body stood braced instead of falling (hips down at frame " + std::to_string(stiffFallen) + ")");
        std::cout << "Ragdoll muscle tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
