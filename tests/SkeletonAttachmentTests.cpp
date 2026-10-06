#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/SkeletonAttachmentComponent.h"

#include <iostream>
#include <memory>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>

int main()
{
    using namespace PlutoGE::scene;

    Scene scene;
    auto ownerStorage = std::make_unique<Entity>(EntityConfig{.name = "Skinned mesh"});
    auto *owner = scene.AddEntity(std::move(ownerStorage));
    auto *mesh = owner->CreateComponent<MeshComponent>(MeshComponentConfig{});

    auto rootStorage = std::make_unique<Entity>(EntityConfig{.name = "Root bone"});
    auto *root = scene.AddEntity(std::move(rootStorage), owner);
    root->CreateComponent<SkeletonAttachmentComponent>(0, "root");
    const EntityID rootId = root->GetID();

    auto spineStorage = std::make_unique<Entity>(EntityConfig{.name = "Spine bone"});
    auto *spine = scene.AddEntity(std::move(spineStorage), root);
    spine->CreateComponent<SkeletonAttachmentComponent>(1, "spine");
    const EntityID spineId = spine->GetID();

    auto handStorage = std::make_unique<Entity>(EntityConfig{.name = "Hand attachment"});
    auto *hand = scene.AddEntity(std::move(handStorage), spine);
    hand->CreateComponent<SkeletonAttachmentComponent>(2, "hand_r");
    const EntityID handId = hand->GetID();

    auto weaponStorage = std::make_unique<Entity>(EntityConfig{.name = "Weapon"});
    auto *weapon = scene.AddEntity(std::move(weaponStorage), hand);
    const EntityID weaponId = weapon->GetID();

    const std::size_t removed = mesh->CompactSkeletonAttachmentEntities();
    if (removed != 2 || scene.FindEntityByID(rootId) || scene.FindEntityByID(spineId))
    {
        std::cerr << "Unused skeleton attachment entities were not removed.\n";
        return 1;
    }
    if (scene.FindEntityByID(handId) != hand || hand->GetParent() != owner ||
        scene.FindEntityByID(weaponId) != weapon || weapon->GetParent() != hand)
    {
        std::cerr << "Gameplay attachment hierarchy was not preserved.\n";
        return 1;
    }

    PlutoGE::render::MeshConfig meshConfig;
    meshConfig.skeleton.joints.push_back({.name = "root", .nodeIndex = 0});
    meshConfig.animationNodes.push_back({.name = "root"});
    PlutoGE::render::Mesh skinnedMesh(meshConfig);

    auto animationOwnerStorage = std::make_unique<Entity>(EntityConfig{.name = "Animated character"});
    auto *animationOwner = scene.AddEntity(std::move(animationOwnerStorage));
    auto *animation = animationOwner->CreateComponent<AnimationComponent>();
    auto meshOwnerStorage = std::make_unique<Entity>(EntityConfig{.name = "Character mesh"});
    auto *meshOwner = scene.AddEntity(std::move(meshOwnerStorage), animationOwner);
    meshOwner->CreateComponent<MeshComponent>(MeshComponentConfig{.mesh = &skinnedMesh});

    if (!animation->IsJointPoseDirty())
    {
        std::cerr << "New animation pose unexpectedly started clean.\n";
        return 1;
    }
    animation->Update(0.0f);
    if (animation->IsJointPoseDirty())
    {
        std::cerr << "Animation update did not eagerly evaluate its skeleton pose.\n";
        return 1;
    }

    // Deliberately use a non-topological skin joint order. Finger children must
    // follow the solved hand; skinning and socket readers share the palette.
    PlutoGE::render::Skeleton arm;
    arm.joints.resize(4);
    arm.joints[0].name = "hand"; arm.joints[0].parentJointIndex = 2;
    arm.joints[1].name = "upper";
    arm.joints[2].name = "lower"; arm.joints[2].parentJointIndex = 1;
    arm.joints[3].name = "finger"; arm.joints[3].parentJointIndex = 0;
    arm.joints[2].localBindTransform = glm::translate(glm::mat4(1), glm::vec3(1,0,0));
    arm.joints[0].localBindTransform = glm::translate(glm::mat4(1), glm::vec3(1,0,0));
    arm.joints[3].localBindTransform = glm::translate(glm::mat4(1), glm::vec3(.2f,0,0));
    arm.joints[0].inverseBindMatrix = glm::translate(glm::mat4(1), glm::vec3(-2,0,0));
    arm.joints[2].inverseBindMatrix = glm::translate(glm::mat4(1), glm::vec3(-1,0,0));
    arm.joints[3].inverseBindMatrix = glm::translate(glm::mat4(1), glm::vec3(-2.2f,0,0));
    auto original = std::vector<glm::mat4>(4, glm::mat4(1));
    auto palette = original;
    TwoBoneIKTarget ik { .root="upper", .middle="lower", .tip="hand", .position={1,1,0}, .pole={0,1,0} };
    auto position = [&](int i) { return glm::vec3((palette[i]*glm::inverse(arm.joints[i].inverseBindMatrix))[3]); };
    if (!SolveTwoBoneIK(arm, palette, ik) || glm::length(position(0)-ik.position) > .0001f ||
        std::abs(glm::length(position(2)-position(1))-1) > .0001f ||
        std::abs(glm::length(position(0)-position(2))-1) > .0001f ||
        std::abs(glm::length(position(3)-position(0))-.2f) > .0001f)
    { std::cerr << "IK reach, bone length or descendant propagation failed.\n"; return 1; }
    palette = original; ik.position={100,0,0};
    if (!SolveTwoBoneIK(arm, palette, ik) || std::abs(glm::length(position(0))-2) > .0001f)
    { std::cerr << "Unreachable IK target stretched the arm.\n"; return 1; }
    palette=original; ik.position={0,0,0}; ik.pole={0,0,0};
    if (!SolveTwoBoneIK(arm, palette, ik) || !std::isfinite(position(0).x))
    { std::cerr << "Degenerate IK target produced invalid pose.\n"; return 1; }
    palette=original; ik.weight=0; ik.position={1,1,0};
    if (!SolveTwoBoneIK(arm, palette, ik) || palette != original) return 1;
    ik.weight=1; ik.middle="missing";
    if (SolveTwoBoneIK(arm, palette, ik) || palette != original) return 1;
    ik.middle="lower"; ik.position.x=std::numeric_limits<float>::quiet_NaN();
    if (SolveTwoBoneIK(arm, palette, ik) || palette != original) return 1;
    palette=original; ik.position={1,1,0}; ik.pole={0,1,0}; ik.weight=.5f;
    if (!SolveTwoBoneIK(arm, palette, ik) || glm::length(position(0)-glm::vec3(2,0,0)) < .01f ||
        glm::length(position(0)-ik.position) < .01f || std::abs(glm::length(position(0)-position(2))-1) > .0001f) return 1;
    palette=original; ik.weight=1; ik.rotation=glm::angleAxis(.7f, glm::vec3(0,0,1)); ik.rotationWeight=1;
    if (!SolveTwoBoneIK(arm, palette, ik)) return 1;
    const auto handMatrix=palette[0]*glm::inverse(arm.joints[0].inverseBindMatrix);
    if (std::abs(glm::dot(glm::normalize(glm::quat_cast(glm::mat3(handMatrix))), ik.rotation)) < .9999f) return 1;
    ik.rotationWeight=0;
    // Integration: an AnimationComponent without clips can pose the bind rig.
    AnimationComponent procedural;
    ik.position={1,1,0}; ik.pole={0,1,0};
    procedural.SetTwoBoneIK("left", ik);
    palette=procedural.GetJointMatrices(arm);
    if (glm::length(position(0)-ik.position) > .0001f) return 1;
    // The node hierarchy path must also consume the post-animation IK palette.
    std::vector<PlutoGE::render::AnimationNode> armNodes(4);
    for (int i=0; i<4; ++i) {
        arm.joints[i].nodeIndex=i;
        armNodes[i].name=arm.joints[i].name;
        armNodes[i].parentNodeIndex=arm.joints[i].parentJointIndex;
        armNodes[i].localBindTransform=arm.joints[i].localBindTransform;
    }
    procedural.SetTwoBoneIK("left", ik);
    palette=procedural.GetJointMatrices(arm, armNodes);
    if (glm::length(position(0)-ik.position) > .0001f) return 1;
    procedural.ClearAllTwoBoneIK();
    palette=procedural.GetJointMatrices(arm);
    if (glm::length(position(0)-glm::vec3(2,0,0)) > .0001f) return 1;
    return 0;
}
