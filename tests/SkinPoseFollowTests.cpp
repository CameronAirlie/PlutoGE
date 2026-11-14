// Skinned meshes posed by their parent's skeleton (clothing): how follower joints match source joints (SkinPoseFollow.h),
// that follower geometry lands where the body's would, and that MeshComponent draws a follower from its parent's pose
// without disturbing the parent's animator.
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/RenderSceneFrontend.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SkinPoseFollow.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace PlutoGE;

namespace
{
    void Require(bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    glm::mat4 At(glm::vec3 position) { return glm::translate(glm::mat4(1.0f), position); }

    // hips -> spine -> neck -> head, and spine -> arm: bind positions in mesh space.
    render::Skeleton Body()
    {
        render::Skeleton skeleton;
        const char *names[] = {"hips", "spine", "neck", "head", "arm"};
        const int parents[] = {-1, 0, 1, 2, 1};
        const glm::vec3 bind[] = {{0, 0, 0}, {0, 1, 0}, {0, 2, 0}, {0, 3, 0}, {1, 1, 0}};
        for (int i = 0; i < 5; ++i)
            skeleton.joints.push_back({.name = names[i], .parentJointIndex = parents[i], .inverseBindMatrix = At(-bind[i])});
        skeleton.humanoidBoneMappings = {{.bone = render::HumanoidBone::Head, .targetJointIndex = 3}};
        return skeleton;
    }

    // A coat exported on its own: only the joints it is weighted to, in another order, the head under an alias, a
    // coat-tail bone the body lacks, and the arm bound 10 cm higher than the body's.
    render::Skeleton Coat()
    {
        render::Skeleton skeleton;
        skeleton.joints.push_back({.name = "arm", .parentJointIndex = 1, .inverseBindMatrix = At(-glm::vec3(1, 1.1f, 0))});
        skeleton.joints.push_back({.name = "spine", .parentJointIndex = -1, .inverseBindMatrix = At(-glm::vec3(0, 1, 0))});
        skeleton.joints.push_back({.name = "tail", .parentJointIndex = 1, .inverseBindMatrix = At(-glm::vec3(0, 0.5f, -0.3f))});
        skeleton.joints.push_back({.name = "Head", .parentJointIndex = 1, .inverseBindMatrix = At(-glm::vec3(0, 3, 0))});
        skeleton.joints.push_back({.name = "loose", .parentJointIndex = -1});
        return skeleton;
    }

    // The body posed: everything moved 5 m along x, the spine (and all above it) turned 90 degrees about y.
    std::vector<glm::mat4> PosedBody(const render::Skeleton &body)
    {
        const glm::mat4 move = At({5, 0, 0});
        const glm::mat4 turn = At({0, 1, 0}) * glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 1, 0)) *
                               At({0, -1, 0});
        std::vector<glm::mat4> palette(body.joints.size(), move);
        for (int joint : {1, 2, 3, 4})
            palette[joint] = move * turn;
        return palette;
    }

    glm::vec3 Skin(const glm::mat4 &palette, glm::vec3 bindPosition) { return glm::vec3(palette * glm::vec4(bindPosition, 1.0f)); }
    bool Near(glm::vec3 a, glm::vec3 b) { return glm::length(a - b) < 1e-4f; }

    render::MeshConfig SkinnedConfig(const render::Skeleton &skeleton)
    {
        render::MeshConfig config;
        config.skeleton = skeleton;
        config.animationNodes.resize(skeleton.joints.size());
        for (size_t i = 0; i < skeleton.joints.size(); ++i)
        {
            auto &joint = config.skeleton.joints[i];
            joint.nodeIndex = static_cast<int>(i);
            const int parent = joint.parentJointIndex;
            const glm::mat4 global = glm::inverse(joint.inverseBindMatrix);
            const glm::mat4 parentGlobal = parent >= 0 ? glm::inverse(skeleton.joints[parent].inverseBindMatrix) : glm::mat4(1.0f);
            joint.localBindTransform = glm::inverse(parentGlobal) * global;
            config.animationNodes[i] = {.name = joint.name, .parentNodeIndex = parent, .localBindTransform = joint.localBindTransform};
        }
        return config;
    }
}

int main()
{
    try
    {
        const auto body = Body();
        const auto coat = Coat();
        {
            const auto map = scene::BuildSkinPoseFollowMap(body, coat);
            Require(map.sourceJoint == std::vector<int>({4, 1, -1, 3, -1}), "joints match by name, then by humanoid alias");
            Require(map.anchor == std::vector<int>({0, 1, 1, 3, -1}), "an unmatched joint follows its nearest matched ancestor");
            Require(map.matchedJoints == 3, "three coat joints match the body");
            Require(map.bindOffset[1] == glm::mat4(1.0f), "joints bound alike need no offset");

            const auto bodyPalette = PosedBody(body);
            std::vector<glm::mat4> palette;
            scene::ApplySkinPoseFollow(map, bodyPalette, palette);
            Require(palette.size() == coat.joints.size(), "one matrix per follower joint");
            // A point of the coat at a bind position goes where the body's point at that position goes.
            const glm::vec3 chest(0.2f, 1.5f, 0.3f);
            Require(Near(Skin(palette[1], chest), Skin(bodyPalette[1], chest)), "the coat's chest moves with the body's chest");
            Require(Near(Skin(palette[3], {0, 3.2f, 0}), Skin(bodyPalette[3], {0, 3.2f, 0})), "the hood follows the aliased head");
            // The coat was bound with its arm joint 10 cm higher (another rest pose): posed, the coat's joint takes the
            // body joint's transform, so geometry at the coat's joint lands on the body's posed joint.
            const glm::vec3 posedBodyArm = Skin(bodyPalette[4], {1, 1, 0});
            Require(Near(Skin(palette[0], {1, 1.1f, 0}), posedBodyArm), "a joint bound in another rest pose takes the body's pose");
            Require(Near(Skin(palette[0], {1.5f, 1.1f, 0}) - posedBodyArm, Skin(bodyPalette[4], {1.5f, 1, 0}) - posedBodyArm),
                    "geometry keeps its place relative to its joint");
            Require(Near(Skin(palette[2], {0, 0.5f, -0.3f}), Skin(bodyPalette[1], {0, 0.5f, -0.3f})),
                    "the coat tail moves rigidly with the spine");
            Require(palette[4] == glm::mat4(1.0f), "a joint with nothing to follow stays at its bind pose");

            std::vector<glm::mat4> shortPalette;
            scene::ApplySkinPoseFollow(map, std::vector<glm::mat4>(2, At({9, 9, 9})), shortPalette);
            Require(shortPalette[0] == glm::mat4(1.0f), "a source palette too short for a joint leaves it at bind");
        }
        {
            render::Mesh bodyMesh(SkinnedConfig(body));
            render::Mesh coatMesh(SkinnedConfig(coat));
            render::Material material({.color = {1, 1, 1, 1}});
            scene::Scene scene;
            // Rig (animator) -> Cape (a follower, first child), Body (the skinned body) -> Coat (a follower of the body).
            // A depth-first search for the rig's mesh meets the cape before the body.
            auto *rig = scene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Rig"}));
            auto *animation = rig->CreateComponent<scene::AnimationComponent>();
            auto *cape = scene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Cape"}), rig)
                             ->CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{.mesh = &coatMesh});
            cape->SetFollowParentSkeleton(true);
            auto *character = scene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Body"}), rig);
            auto *bodyComponent = character->CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{.mesh = &bodyMesh});
            bodyComponent->SetMaterial(&material);
            auto *coatEntity = scene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Coat"}), character);
            auto *coatComponent = coatEntity->CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{.mesh = &coatMesh});
            coatComponent->SetMaterial(&material);
            Require(!cape->FindFollowedSkeletonMesh(), "a follower whose parents have no skinned mesh follows nothing");

            Require(!coatComponent->FindFollowedSkeletonMesh(), "a mesh follows nothing until asked");
            coatComponent->SetFollowParentSkeleton(true);
            Require(coatComponent->FindFollowedSkeletonMesh() == bodyComponent, "the follower finds the skinned parent");
            Require(coatComponent->NeedsFrameRenderSubmission(), "a follower is drawn every frame like its parent");
            const auto pose = animation->PreparePoseEvaluation();
            Require(pose && pose->skeleton == &bodyMesh.GetSkeleton(), "the animator poses the body, never the follower");

            auto &frontend = core::Engine::GetInstance().GetRenderScene();
            const auto submit = [&](scene::MeshComponent *component) {
                frontend.ClearRenderCommands();
                component->SubmitRenderCommands();
                return frontend.GetSceneRenderCommands();
            };
            const auto bodyCommands = submit(bodyComponent);
            Require(bodyCommands.size() == 1 && bodyCommands[0].jointMatrices, "the body draws skinned");
            const auto bodyPalette = *bodyCommands[0].jointMatrices;
            const auto revision = animation->GetJointPoseRevision();
            const auto coatCommands = submit(coatComponent);
            Require(coatCommands.size() == 1 && coatCommands[0].jointMatrices, "the coat draws skinned");
            Require(animation->GetJointPoseRevision() == revision, "drawing the coat does not re-pose the body");
            Require(coatComponent->GetFollowedJointCount() == 3, "the coat's joints matched the body's");
            const auto &coatPalette = *coatCommands[0].jointMatrices;
            Require(coatPalette.size() == coat.joints.size(), "the coat's palette fits its own skeleton");
            const glm::vec3 chest(0.2f, 1.5f, 0.3f);
            Require(Near(Skin(coatPalette[1], chest), Skin(bodyPalette[1], chest)), "the coat is drawn in the body's pose");
            Require(*submit(bodyComponent)[0].jointMatrices == bodyPalette, "the body's own draw is unchanged");

            const auto saved = coatComponent->Serialize();
            Require(std::ranges::any_of(saved, [](const auto &p) { return p.name == "Follow Parent Skeleton" && p.value == "true"; }),
                    "following is saved");
            Require(std::ranges::none_of(bodyComponent->Serialize(), [](const auto &p) { return p.name == "Follow Parent Skeleton"; }),
                    "ordinary meshes save as before");
            auto *loaded = scene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Loaded"}), character)
                               ->CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{.mesh = &coatMesh});
            loaded->Deserialize(saved);
            Require(loaded->GetFollowParentSkeleton(), "following loads back");

            // Off a skinned parent the coat draws in its bind pose rather than in some other animator's pose.
            coatEntity->SetParent(nullptr);
            Require(!coatComponent->FindFollowedSkeletonMesh(), "a follower without a skinned parent follows nothing");
            const auto unparented = submit(coatComponent);
            Require(unparented.size() == 1 && !unparented[0].jointMatrices, "a follower without a parent draws in bind pose");
        }
        std::cout << "SkinPoseFollowTests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "SkinPoseFollowTests failed: " << error.what() << '\n';
        return 1;
    }
}
