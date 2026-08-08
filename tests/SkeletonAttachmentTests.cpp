#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/SkeletonAttachmentComponent.h"

#include <cassert>
#include <iostream>
#include <memory>

int main()
{
    using namespace PlutoGE::scene;

    {
        MeshComponent reloadedMesh(MeshComponentConfig{});
        reloadedMesh.Deserialize({
            {"MeshAssetReference", PropertyType::String, std::string(PlutoGE::assets::Project::kBuiltinCubeMeshReference)},
        });
        assert(reloadedMesh.GetMesh() != nullptr);
        assert(reloadedMesh.GetMeshAssetReference() == PlutoGE::assets::Project::kBuiltinCubeMeshReference);
    }

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

    return 0;
}
