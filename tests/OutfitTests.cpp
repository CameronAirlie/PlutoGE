// Clothing: skin hidden under worn items (SkinCoverage.h), whether a garment was made for a body (SkinFitReport),
// transient entities (never saved, copied or written into prefabs), and OutfitComponent putting wearable prefabs on a
// body: one per slot, following its skeleton, hiding the skin under them, rebuilt after an undo, taken off cleanly.
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/RenderSceneFrontend.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/SkinCoverage.h"
#include "PlutoGE/scene/SkinPoseFollow.h"
#include "PlutoGE/scene/WearablePrefab.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/OutfitComponent.h"
#include "PlutoGE/scene/components/WearableComponent.h"
#include "OutfitTestFixtures.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace PlutoGE;

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    using namespace outfit_fixtures;

    void TestCoverage()
    {
        render::Mesh body(SkinnedConfig(Body()));
        const std::vector<std::string> spine{"spine"};
        std::size_t hidden = 0;
        auto covered = scene::BuildCoveredMesh(body, scene::ResolveCoveredJoints(body.GetSkeleton(), spine), scene::CoveredSkinThreshold, &hidden);
        Require(covered && hidden == 2, "the spine's own two triangles are hidden (" + std::to_string(hidden) + ")");
        Require(covered->GetSubmesh(0).indexCount == body.GetSubmesh(0).indexCount - 6, "the submesh range shrinks by them");
        Require(covered->GetSubmesh(0).lods.size() == 1 && covered->GetSubmesh(0).lods[0].indexCount == covered->GetSubmesh(0).indexCount,
                "a LOD sharing the range shrinks with it");
        Require(covered->GetMeshData().vertices.size() == body.GetMeshData().vertices.size() &&
                covered->GetSkeleton().joints.size() == 4, "vertices and skeleton are kept, so the copy skins alike");
        const std::vector<std::string> head{"Head"};
        Require(scene::BuildCoveredMesh(body, scene::ResolveCoveredJoints(body.GetSkeleton(), head)) != nullptr,
                "covered joints resolve humanoid aliases");
        const std::vector<std::string> none{"tail"};
        Require(!scene::BuildCoveredMesh(body, scene::ResolveCoveredJoints(body.GetSkeleton(), none)), "covering nothing needs no copy");
        const std::vector<std::string> both{"spine", "head"};
        scene::BuildCoveredMesh(body, scene::ResolveCoveredJoints(body.GetSkeleton(), both), scene::CoveredSkinThreshold, &hidden);
        Require(hidden == 5, "a triangle across two covered joints is hidden too");
    }

    void TestFit()
    {
        const auto body = Body();
        const auto assess = [&](const render::Skeleton &garment)
        { return scene::AssessSkinFit(body, garment, scene::BuildSkinPoseFollowMap(body, garment)); };
        const auto same = assess(Body());
        Require(same.quality == scene::SkinFitQuality::Fits && same.matchedByName == 4 && same.largestProportionError < 1e-4f,
                "a garment made on the body fits");
        const auto otherRestPose = assess(Body(1.0f, {"hips", "spine", "head", "arm"}, glm::radians(40.0f)));
        Require(otherRestPose.quality == scene::SkinFitQuality::Fits, "a garment bound in another rest pose (A-pose) still fits");
        const auto taller = assess(Body(1.06f));
        Require(taller.quality == scene::SkinFitQuality::OtherBody && taller.largestProportionError > 0.03f,
                "a garment made for a taller body is reported (" + std::to_string(taller.largestProportionError) + " m at " + taller.worstJoint + ")");
        const auto slightly = assess(Body(1.015f));
        Require(slightly.quality == scene::SkinFitQuality::Loose, "a couple of centimetres is loose");
        const auto otherRig = assess(Body(1.0f, {"Hips", "Spine", "Head", "Arm"}));
        Require(otherRig.matchedByName == 0 && otherRig.matchedByMapping == 1 && otherRig.matchedByAlias == 3,
                "another rig matches by its humanoid mapping and by aliases, never by name");
        auto stranger = Body(1.0f, {"a", "b", "c", "d"});
        stranger.humanoidBoneMappings.clear();
        Require(assess(stranger).quality == scene::SkinFitQuality::Unmatched, "a garment sharing no joints cannot follow");
    }

    void TestTransient(const std::filesystem::path &root)
    {
        scene::Scene world;
        auto *parent = world.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Kept"}));
        auto *generated = world.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Generated"}), parent);
        world.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = "GeneratedChild"}), generated);
        generated->SetTransient(true);
        Require(generated->IsTransientInHierarchy() && generated->GetChildren().front()->IsTransientInHierarchy() &&
                !parent->IsTransientInHierarchy(), "transience covers an entity's subtree");
        std::string text, error;
        Require(scene::SceneSerializer::SaveToString(world, text, &error), error);
        Require(text.find("Kept") != std::string::npos && text.find("Generated") == std::string::npos,
                "a scene saves without transient entities");
        const auto prefab = root / "Kept.plutoprefab";
        Require(scene::Prefab::SaveFromEntity(*parent, prefab, &error), error);
        std::ifstream prefabFile(prefab);
        const std::string prefabText{std::istreambuf_iterator<char>(prefabFile), {}};
        Require(prefabText.find("Generated") == std::string::npos, "a prefab saved from an entity leaves them out");
        auto *copy = scene::Prefab::DuplicateEntity(world, *parent, nullptr, false);
        Require(copy && copy->GetChildren().empty(), "duplicating an entity leaves them out");
    }

    struct Fixture
    {
        std::filesystem::path root;
        std::string bodyMesh, coatPrefab, cloakPrefab, hoodPrefab, tallPrefab, bootsPrefab, shirtPrefab;
    };

    // A temporary project: the body's mesh, and wearables: a coat and a cloak (both slot Body), a hood (Head) and a
    // coat made for a taller body.
    Fixture WriteProject(const std::filesystem::path &root)
    {
        auto &manager = core::Engine::GetInstance().GetAssetManager();
        manager.SetProjectContext(root.string(), ".");
        const std::vector<std::string> material{std::string(assets::Project::kBuiltinDefaultMaterialReference)};
        Fixture fixture{.root = root, .bodyMesh = "project://Body.plutomesh"};
        std::string error;
        Require(manager.SaveMeshAsset(fixture.bodyMesh, SkinnedConfig(Body()), material, &error), "body mesh: " + error);
        Require(manager.SaveMeshAsset("project://Tall.plutomesh", SkinnedConfig(Body(1.08f)), material, &error), "tall mesh: " + error);
        const auto wearable = [&](const char *name, const char *slot, std::vector<std::string> covers, const char *mesh, bool hides = true)
        {
            scene::Scene authoring;
            auto *entity = authoring.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = name}));
            auto *wear = entity->CreateComponent<scene::WearableComponent>();
            wear->SetSlot(slot);
            wear->SetHidesCoveredSkin(hides);
            wear->SetCoveredJoints(std::move(covers));
            auto *meshComponent = entity->CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{});
            meshComponent->Deserialize({{"MeshAssetReference", scene::PropertyType::String, mesh}});
            const auto path = root / (std::string(name) + ".plutoprefab");
            std::string saveError;
            Require(scene::Prefab::SaveFromEntity(*entity, path, &saveError), saveError);
            return path.string();
        };
        fixture.coatPrefab = wearable("Coat", "Body", {"spine"}, "project://Body.plutomesh");
        fixture.cloakPrefab = wearable("Cloak", "Body", {"spine", "arm"}, "project://Body.plutomesh");
        fixture.hoodPrefab = wearable("Hood", "Head", {"head"}, "project://Body.plutomesh");
        fixture.tallPrefab = wearable("TallCoat", "Body", {}, "project://Tall.plutomesh");
        // Joints ticked but hiding left off (the default): the body stays whole.
        fixture.shirtPrefab = wearable("Shirt", "Body", {"spine", "arm"}, "project://Body.plutomesh", false);

        // The editor's "Create Wearable": a prefab straight from a skinned mesh, its slot guessed from the name.
        Require(scene::GuessWearableSlot("Leather_Boots") == "Feet" && scene::GuessWearableSlot("WoolHood") == "Head" &&
                scene::GuessWearableSlot("Tunic") == "Body", "slots are guessed from names");
        const auto boots = root / "Boots.plutoprefab";
        Require(scene::SaveWearablePrefab("project://Body.plutomesh", "Leather_Boots", boots, &error), "wearable prefab: " + error);
        fixture.bootsPrefab = boots.string();
        auto rigid = SkinnedConfig(Body());
        rigid.skeleton = {};
        Require(manager.SaveMeshAsset("project://Rock.plutomesh", rigid, material, &error), "rock mesh: " + error);
        Require(!scene::SaveWearablePrefab("project://Rock.plutomesh", "Rock", root / "Rock.plutoprefab", &error) && !error.empty() &&
                !std::filesystem::exists(root / "Rock.plutoprefab"), "a mesh without a skeleton cannot be a wearable");
        return fixture;
    }

    void TestOutfit(const Fixture &fixture)
    {
        auto &manager = core::Engine::GetInstance().GetAssetManager();
        scene::Scene world;
        auto *character = world.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Character"}));
        character->CreateComponent<scene::AnimationComponent>();
        auto *bodyEntity = world.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Body"}), character);
        auto *body = bodyEntity->CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{.mesh = manager.LoadMeshAsset(fixture.bodyMesh)});
        Require(body->GetMesh() && body->GetMesh()->HasSkeleton(), "the body mesh loads");
        render::Material skin({.color = {1, 1, 1, 1}});
        body->SetMaterial(&skin);
        auto *outfit = character->CreateComponent<scene::OutfitComponent>();
        Require(outfit->FindBody() == body, "the outfit dresses the skinned mesh below it");

        const auto item = [&](std::size_t index) -> const scene::OutfitComponent::WornItem & { return outfit->GetWornItems().at(index); };
        const auto entityOf = [&](std::size_t index) { return world.FindEntityByID(item(index).instance); };

        outfit->SetWearables({fixture.coatPrefab, fixture.hoodPrefab});
        world.Update(0.0f);
        Require(outfit->GetWornItems().size() == 2 && item(0).state == scene::OutfitComponent::ItemState::Worn &&
                item(1).state == scene::OutfitComponent::ItemState::Worn, "the coat and the hood are worn");
        auto *coat = entityOf(0);
        Require(coat && coat->GetParent() == bodyEntity && coat->IsTransient() && coat->HasTag("Worn"),
                "a worn item is a transient child of the body");
        Require(coat->GetComponent<scene::MeshComponent>()->GetFollowParentSkeleton() &&
                coat->GetComponent<scene::MeshComponent>()->FindFollowedSkeletonMesh() == body, "its mesh follows the body");
        Require(item(0).slot == "Body" && item(1).slot == "Head", "slots come from the wearables");
        Require(item(0).fit.quality == scene::SkinFitQuality::Fits, "a garment made on the body fits it");
        const std::vector<std::string> covered{"head", "spine"};
        Require(body->GetCoveredJoints() == covered, "the skin under both is hidden");
        auto &frontend = core::Engine::GetInstance().GetRenderScene();
        frontend.ClearRenderCommands();
        body->SubmitRenderCommands();
        const auto commands = frontend.GetSceneRenderCommands();
        Require(!commands.empty() && commands.front().mesh != body->GetMesh() && body->GetCoveredTriangleCount() == 5,
                "the body draws without its covered triangles");

        outfit->SetWearables({fixture.shirtPrefab});
        world.Update(0.0f);
        Require(item(0).state == scene::OutfitComponent::ItemState::Worn && body->GetCoveredJoints().empty() &&
                body->GetCoveredTriangleCount() == 0, "a wearable that does not opt in leaves the body whole");
        outfit->SetWearables({fixture.coatPrefab, fixture.hoodPrefab});
        world.Update(0.0f);
        Require(body->GetCoveredJoints() == covered, "opted-in skin hides again");

        std::string text, error;
        Require(scene::SceneSerializer::SaveToString(world, text, &error), error);
        Require(text.find("Coat") != std::string::npos && text.find("\tCoat\t") == std::string::npos &&
                text.find("Wearable Count\t1\t2") != std::string::npos, "a scene saves the list, not the spawned items");

        // A prefab made with SaveWearablePrefab is worn like any other.
        Require(outfit->Wear(fixture.bootsPrefab), "the boots go on");
        world.Update(0.0f);
        Require(item(2).state == scene::OutfitComponent::ItemState::Worn && item(2).slot == "Feet" &&
                entityOf(2)->GetComponent<scene::MeshComponent>()->FindFollowedSkeletonMesh() == body,
                "a created wearable takes the guessed slot and follows the body");
        Require(outfit->TakeOff(fixture.bootsPrefab), "the boots come off");
        world.Update(0.0f);

        // Wear() puts an item on over whatever shows in its slot; taking it off shows that again.
        Require(outfit->Wear(fixture.cloakPrefab) && !outfit->Wear(fixture.cloakPrefab), "the cloak goes on, once");
        world.Update(0.0f);
        Require(item(0).state == scene::OutfitComponent::ItemState::Covered && !entityOf(0)->IsSelfActive() &&
                item(2).state == scene::OutfitComponent::ItemState::Worn, "the cloak covers the coat");
        Require(std::ranges::find(body->GetCoveredJoints(), "arm") != body->GetCoveredJoints().end(), "the cloak hides the arm's skin");
        Require(outfit->TakeOff(fixture.cloakPrefab), "the cloak comes off");
        world.Update(0.0f);
        Require(item(0).state == scene::OutfitComponent::ItemState::Worn && entityOf(0)->IsSelfActive() &&
                std::ranges::find(body->GetCoveredJoints(), "arm") == body->GetCoveredJoints().end(), "and the coat shows again");

        // An authored list follows the same rule.
        outfit->SetWearables({fixture.coatPrefab, fixture.cloakPrefab});
        world.Update(0.0f);
        Require(item(0).state == scene::OutfitComponent::ItemState::Covered && !entityOf(0)->IsSelfActive() &&
                item(1).state == scene::OutfitComponent::ItemState::Worn && entityOf(1)->IsSelfActive(),
                "an earlier item in the same slot is covered, not removed");

        // Something removes a worn item (an undo restoring the scene): the outfit puts it back.
        const auto cloakId = item(1).instance;
        world.DestroyEntity(cloakId);
        world.Update(0.0f);
        world.Update(0.0f);
        Require(item(1).state == scene::OutfitComponent::ItemState::Worn && item(1).instance != cloakId && entityOf(1),
                "a worn item removed by something else is spawned again");
        const auto childCount = bodyEntity->GetChildren().size();
        auto *rebuilt = character->CreateComponent<scene::OutfitComponent>();
        rebuilt->Deserialize(outfit->Serialize());
        character->RemoveComponent(outfit);
        world.Update(0.0f);
        world.Update(0.0f);
        Require(bodyEntity->GetChildren().size() == childCount && rebuilt->GetWornItems().size() == 2,
                "a rebuilt outfit takes over the items on the body instead of doubling them");
        outfit = rebuilt;

        // A garment made for another body is worn but reported.
        outfit->SetWearables({fixture.tallPrefab});
        world.Update(0.0f);
        Require(item(0).state == scene::OutfitComponent::ItemState::Worn && item(0).fit.quality == scene::SkinFitQuality::OtherBody,
                "a garment made for a taller body is reported");
        // As the inspector writes a list with a row added but not yet chosen.
        outfit->Deserialize({{"Wearable Count", scene::PropertyType::Int, "2"},
                             {"Wearables.0", scene::PropertyType::String, (fixture.root / "Missing.plutoprefab").string()},
                             {"Wearables.1", scene::PropertyType::String, ""}});
        world.Update(0.0f);
        Require(item(0).state == scene::OutfitComponent::ItemState::Failed && !item(0).error.empty() &&
                item(1).state == scene::OutfitComponent::ItemState::Failed, "a missing or unchosen prefab is reported");

        // Taking everything off, removing the outfit, or disabling it leaves the body bare.
        outfit->SetWearables({fixture.coatPrefab});
        world.Update(0.0f);
        Require(!body->GetCoveredJoints().empty(), "the coat is on again");
        outfit->SetEnabled(false);
        world.Update(0.0f);
        Require(body->GetCoveredJoints().empty() && !world.IsEntityAlive(item(0).instance), "a disabled outfit takes its items off");
        outfit->SetEnabled(true);
        world.Update(0.0f);
        Require(item(0).instance && world.IsEntityAlive(item(0).instance), "enabled again, it puts them back on");
        character->RemoveComponent(outfit);
        world.Update(0.0f);
        Require(body->GetCoveredJoints().empty() &&
                std::ranges::none_of(bodyEntity->GetChildren(), [](const scene::Entity *child) { return child->HasTag("Worn"); }),
                "a removed outfit leaves nothing behind");
    }
}

int main()
{
    const auto root = std::filesystem::temp_directory_path() / "PlutoGEOutfitTests";
    try
    {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        TestCoverage();
        TestFit();
        TestTransient(root);
        TestOutfit(WriteProject(root));
        std::filesystem::remove_all(root);
        std::cout << "OutfitTests passed\n";
        return 0;
    }
    catch (const std::exception &failure)
    {
        std::cerr << "OutfitTests failed: " << failure.what() << '\n';
        return 1;
    }
}
