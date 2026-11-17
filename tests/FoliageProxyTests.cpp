// Foliage proxies (FoliageProxies.h) and the incremental foliage physics they rely on: promoting an instance swaps it
// for a prefab entity at the same transform, releasing puts it back, and neither disturbs the rest of the physics world.
// Interaction tags and the radius queries (FoliageQueries.h) let gameplay find instances to promote without rays.
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/platform/ContentPack.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/FoliageProxies.h"
#include "PlutoGE/scene/FoliageQueries.h"
#include "PlutoGE/scene/FoliageTypeAsset.h"
#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/FoliageProxyComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/RigidbodyComponent.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <random>
#include <source_location>
#include <sstream>
#include <stdexcept>

using namespace PlutoGE;

namespace
{
    void Require(bool value, const std::string &message, const std::source_location location = std::source_location::current())
    {
        if (!value) throw std::runtime_error("Line " + std::to_string(location.line()) + ": " + message);
    }

    struct Scratch
    {
        std::filesystem::path root = std::filesystem::temp_directory_path() /
                                     ("PlutoGE-foliage-proxy-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ~Scratch()
        {
            content::UnmountAll();
            std::error_code ignored;
            std::filesystem::remove_all(root, ignored);
        }
    };

    bool Near(const glm::mat4 &a, const glm::mat4 &b, float epsilon = 1e-4f)
    {
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
                if (std::abs(a[column][row] - b[column][row]) > epsilon) return false;
        return true;
    }

    // A tree type with capsule collision, owned by a new entity.
    scene::FoliageComponent &AddForest(scene::Scene &scene, const std::string &proxyPrefab, float cellSize = 16.0f)
    {
        auto *owner = scene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Forest"}));
        auto *foliage = owner->CreateComponent<scene::FoliageComponent>();
        auto &type = foliage->AddType("Pine");
        type.asset.cellSize = cellSize;
        type.asset.collisionEnabled = true;
        type.asset.collisionCenter = {0, 1, 0};
        type.asset.collisionRadius = 0.5f;
        type.asset.collisionHeight = 2.0f;
        type.asset.proxyPrefabReference = proxyPrefab;
        return *foliage;
    }

    void TestAssetAndSceneRoundTrip()
    {
        scene::FoliageTypeAsset asset;
        asset.proxyPrefabReference = "project://Nodes/Pine.plutoprefab";
        std::stringstream file;
        Require(scene::WriteFoliageTypeAsset(file, asset), "Writing a foliage type asset failed");
        Require(file.str().find("PROXY_PREFAB\tproject://Nodes/Pine.plutoprefab") != std::string::npos, "No proxy record written");
        scene::FoliageTypeAsset loaded;
        std::string error;
        Require(scene::ReadFoliageTypeAsset(file, loaded, &error), error);
        Require(loaded.proxyPrefabReference == asset.proxyPrefabReference, "The proxy prefab did not survive the asset file");

        const auto changed = scene::ChangedFoliageTypeProperties({}, asset);
        Require(std::find(changed.begin(), changed.end(), "ProxyPrefab") != changed.end(), "A proxy change was not reported as an override");

        // Older files have no record and load as scenery.
        std::stringstream old("PLUTOFOLIAGE\t2\nCELL_SIZE\t16\n");
        Require(scene::ReadFoliageTypeAsset(old, loaded, &error) && loaded.proxyPrefabReference.empty(), "An old asset gained a proxy");

        scene::Scene scene;
        auto &foliage = AddForest(scene, asset.proxyPrefabReference);
        scene::FoliageComponent restored;
        restored.Deserialize(foliage.Serialize());
        Require(restored.GetType(0) && restored.GetType(0)->asset.proxyPrefabReference == asset.proxyPrefabReference,
                "The proxy prefab did not survive scene serialization");
    }

    void TestCellSignatures()
    {
        scene::Scene scene;
        auto &foliage = AddForest(scene, {});
        auto &type = *foliage.GetType(0);
        type.instances = {scene::FoliageInstance{.id = 1, .position = {1, 0, 1}},
                          scene::FoliageInstance{.id = 2, .position = {2, 0, 2}},
                          scene::FoliageInstance{.id = 3, .position = {40, 0, 1}}};
        foliage.RestoreInstanceSnapshot(foliage.CaptureInstanceSnapshot());
        const auto signatures = [&]
        {
            std::vector<std::pair<int, std::uint64_t>> result;
            for (const auto &cell : foliage.BuildCollisionCells()) result.emplace_back(cell.coordinate.x, cell.signature);
            std::sort(result.begin(), result.end());
            return result;
        };
        const auto before = signatures();
        Require(before.size() == 2 && before[0].second != 0 && before[1].second != 0, "Expected two signed cells");
        const std::array removedId{std::uint64_t{1}};
        const auto removed = foliage.RemoveInstances(removedId);
        const auto after = signatures();
        Require(after.size() == 2 && after[0].second != before[0].second && after[1].second == before[1].second,
                "Removing an instance changed a cell it was not in, or not its own");
        foliage.RestoreInstances(removed);
        Require(signatures() == before, "Restoring the instance did not restore its cell's signature");
    }

    void TestPromoteAndRelease()
    {
        Scratch scratch;
        core::Engine::GetInstance().GetAssetManager().SetProjectContext(scratch.root.string());
        std::string error;
        {
            scene::Scene authoring;
            auto *tree = authoring.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Pine Proxy"}));
            tree->CreateComponent<scene::ColliderComponent>(scene::ColliderComponentConfig{
                .shape = scene::ColliderShape::Capsule, .center = {0, 1, 0}, .radius = 0.5f, .height = 2.0f});
            authoring.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Visual"}), tree);
            Require(scene::Prefab::SaveFromEntity(*tree, scratch.root / "Assets" / "Pine.plutoprefab", &error), error);
        }

        scene::Scene scene;
        auto &foliage = AddForest(scene, "project://Pine.plutoprefab");
        auto &scenery = foliage.AddType("Grass");
        foliage.GetOwner()->SetPosition({5, 0, 0});
        foliage.GetOwner()->SetRotation({0, 90, 0});
        foliage.GetType(0)->instances = {scene::FoliageInstance{.id = 7, .position = {0, 0, 3}, .rotationDegrees = {0, 30, 0}, .scale = {1.5f, 1.5f, 1.5f}},
                                         scene::FoliageInstance{.id = 8, .position = {0, 0, -3}}};
        scenery.instances = {scene::FoliageInstance{.id = 9}};
        foliage.RestoreInstanceSnapshot(foliage.CaptureInstanceSnapshot());

        const auto location = foliage.FindInstance(7);
        Require(location.has_value(), "Instance 7 is missing");
        const auto expected = foliage.GetInstanceWorldTransform(*location);
        Require(expected.has_value(), "No world transform for instance 7");
        const glm::vec3 trunk = glm::vec3((*expected)[3]) + glm::vec3(0, 1, 0);

        scene::PhysicsRaycastHit hit;
        Require(scene.Raycast(trunk + glm::vec3(0, 0, 5), {0, 0, -1}, 10, hit) && hit.foliageInstanceId == 7,
                "The ray did not report the foliage instance it hit");

        auto *proxy = scene::PromoteFoliageInstance(foliage, 7, &error);
        Require(proxy != nullptr, "Promotion failed: " + error);
        Require(Near(proxy->GetWorldTransform(), *expected), "The proxy does not stand where the instance did");
        const auto *link = proxy->GetComponent<scene::FoliageProxyComponent>();
        Require(link && link->GetFoliageEntityId() == foliage.GetOwner()->GetID() && link->GetInstanceId() == 7,
                "The proxy does not record its instance");
        Require(!foliage.FindInstance(7) && foliage.FindInstance(8), "Promotion removed the wrong instances");

        Require(scene.Raycast(trunk + glm::vec3(0, 0, 5), {0, 0, -1}, 10, hit) && hit.entityId == proxy->GetID() &&
                    hit.foliageInstanceId == 0,
                "A ray still reached the promoted instance instead of its proxy");

        Require(!scene::PromoteFoliageInstance(foliage, 7, &error) && !error.empty(), "An instance was promoted twice");
        Require(!scene::PromoteFoliageInstance(foliage, 9, &error) && error.find("no proxy prefab") != std::string::npos,
                "A type without a proxy prefab was promoted");

        const auto proxyId = proxy->GetID();
        Require(scene::ReleaseFoliageProxy(*proxy), "Releasing the proxy did not restore the instance");
        const auto back = foliage.FindInstance(7);
        Require(back && Near(*foliage.GetInstanceWorldTransform(*back), *expected), "The instance came back changed");
        Require(!scene.IsEntityAlive(proxyId), "The released proxy was not destroyed");
        Require(scene.Raycast(trunk + glm::vec3(0, 0, 5), {0, 0, -1}, 10, hit) && hit.foliageInstanceId == 7,
                "The restored instance does not collide");

        // Releasing a proxy whose foliage already holds the instance only destroys the proxy.
        auto *again = scene::PromoteFoliageInstance(foliage, 8, &error);
        Require(again != nullptr, error);
        const std::array removedRecord{again->GetComponent<scene::FoliageProxyComponent>()->GetRemovedInstance()};
        foliage.RestoreInstances(removedRecord);
        Require(!scene::ReleaseFoliageProxy(*again), "A duplicate instance was restored");
        std::size_t copies = 0;
        for (const auto &instance : foliage.GetType(0)->instances) copies += instance.id == 8 ? 1u : 0u;
        Require(copies == 1, "Release duplicated an instance");
    }

    bool HasProperty(const std::vector<scene::Property> &properties, std::string_view name)
    {
        return std::any_of(properties.begin(), properties.end(), [&](const auto &property) { return property.name == name; });
    }

    // A mesh sways like foliage when given a wind response, and a proxy's meshes showing the type's mesh inherit the
    // type's response on promotion, so a swaying bush does not freeze as it becomes an entity.
    void TestProxyKeepsWind()
    {
        constexpr const char *kBushMesh = "engine://builtin/mesh/cube";
        render::FoliageWindResponse sway{.enabled = true, .stiffness = 0.4f, .flutter = 0.6f, .bendHeight = 0.8f, .pivotHeight = 0.1f};

        // The mesh's own setting: saved only when on, always offered to the inspector, read back as written.
        {
            scene::MeshComponent still(scene::MeshComponentConfig{});
            Require(!HasProperty(still.Serialize(), "Wind") && HasProperty(still.SerializeForInspector(), "Wind Stiffness"),
                    "A still mesh saved wind, or the inspector cannot turn it on");
            scene::MeshComponent windy(scene::MeshComponentConfig{});
            windy.SetWind(sway);
            scene::MeshComponent restored(scene::MeshComponentConfig{});
            restored.Deserialize(windy.Serialize());
            Require(restored.GetWind() == sway.Sanitized(), "A mesh's wind did not survive serialization");
        }

        Scratch scratch;
        core::Engine::GetInstance().GetAssetManager().SetProjectContext(scratch.root.string());
        std::string error;
        {
            scene::Scene authoring;
            auto *bush = authoring.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Bush Proxy"}));
            auto *visual = authoring.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Visual"}), bush);
            visual->CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{})->Deserialize({{"MeshAssetReference", scene::PropertyType::String, kBushMesh}});
            auto *berries = authoring.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Berries"}), bush);
            berries->CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{})->Deserialize(
                {{"MeshAssetReference", scene::PropertyType::String, "engine://builtin/mesh/sphere"}});
            Require(scene::Prefab::SaveFromEntity(*bush, scratch.root / "Assets" / "Bush.plutoprefab", &error), error);
        }

        scene::Scene scene;
        auto *owner = scene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Meadow"}));
        auto *foliage = owner->CreateComponent<scene::FoliageComponent>();
        auto &type = foliage->AddType("Bush");
        type.asset.sourceMeshPath = kBushMesh;
        type.asset.wind = sway;
        type.asset.proxyPrefabReference = "project://Bush.plutoprefab";
        type.instances = {scene::FoliageInstance{.id = 4, .position = {2, 0, 3}}};
        foliage->RestoreInstanceSnapshot(foliage->CaptureInstanceSnapshot());

        auto *proxy = scene::PromoteFoliageInstance(*foliage, 4, &error);
        Require(proxy != nullptr, "Promotion failed: " + error);
        const scene::MeshComponent *visualMesh = nullptr, *berryMesh = nullptr;
        for (auto *child : proxy->GetChildren())
        {
            if (child->GetName() == "Visual") visualMesh = child->GetComponent<scene::MeshComponent>();
            if (child->GetName() == "Berries") berryMesh = child->GetComponent<scene::MeshComponent>();
        }
        Require(visualMesh && berryMesh, "The proxy lost its meshes");
        Require(visualMesh->GetWind() == sway.Sanitized(), "The proxy's copy of the plant did not take the type's wind");
        Require(!berryMesh->GetWind().enabled, "A mesh that is not the plant's took the type's wind");
    }

    // A body sliding along the ground past a tree. Removing the tree (promotion does this) must not reset the
    // simulation: the body keeps its motion, and stops colliding with the tree that is gone.
    float SlidePast(bool removeTree, bool removeDistantTree)
    {
        scene::Scene scene;
        auto *ground = scene.AddEntity(std::make_unique<scene::Entity>());
        ground->SetPosition({0, -0.5f, 0});
        ground->CreateComponent<scene::ColliderComponent>(scene::ColliderComponentConfig{.size = {200, 1, 200}});
        auto &foliage = AddForest(scene, {});
        foliage.GetType(0)->instances = {scene::FoliageInstance{.id = 1, .position = {3, 0, 0}},
                                         scene::FoliageInstance{.id = 2, .position = {60, 0, 60}}};
        foliage.RestoreInstanceSnapshot(foliage.CaptureInstanceSnapshot());
        auto *body = scene.AddEntity(std::make_unique<scene::Entity>());
        body->SetPosition({0, 0.51f, 0});
        body->CreateComponent<scene::ColliderComponent>();
        body->CreateComponent<scene::RigidbodyComponent>(
            scene::RigidbodyComponentConfig{.friction = 0.2f, .freezeRotation = true, .velocity = {4, 0, 0}});
        scene.StartRuntime();
        for (int frame = 0; frame < 120; ++frame)
        {
            if (frame == 10)
            {
                std::vector<std::uint64_t> ids;
                if (removeTree) ids.push_back(1);
                if (removeDistantTree) ids.push_back(2);
                foliage.RemoveInstances(ids);
            }
            scene.Update(1.0f / 60.0f);
        }
        const float travelled = body->GetWorldPosition().x;
        scene.StopRuntime();
        return travelled;
    }

    void TestInteractionTags()
    {
        Require(scene::ParseFoliageInteractionTags(" Gather, Fibre,,Gather\tMine ") ==
                    std::vector<std::string>{"Gather", "Fibre", "Mine"},
                "Tags were not split on commas and whitespace, trimmed and deduplicated");
        Require(scene::JoinFoliageInteractionTags({"Gather", "Fibre"}) == "Gather,Fibre", "Tags did not join with commas");

        scene::FoliageTypeAsset asset;
        asset.interactionTags = {"Gather", "Fibre"};
        std::stringstream file;
        Require(scene::WriteFoliageTypeAsset(file, asset), "Writing a foliage type asset failed");
        Require(file.str().find("INTERACTION_TAGS\tGather\tFibre") != std::string::npos, "No tag record written");
        scene::FoliageTypeAsset loaded;
        std::string error;
        Require(scene::ReadFoliageTypeAsset(file, loaded, &error), error);
        Require(loaded.interactionTags == asset.interactionTags, "The tags did not survive the asset file");
        Require(scene::HasFoliageInteractionTag(loaded, "Fibre") && !scene::HasFoliageInteractionTag(loaded, "fibre") &&
                    !scene::HasFoliageInteractionTag(loaded, ""),
                "Tags must match exactly, and an empty tag matches nothing");

        std::stringstream untagged;
        Require(scene::WriteFoliageTypeAsset(untagged, {}) && untagged.str().find("INTERACTION_TAGS") == std::string::npos,
                "An untagged type wrote a tag record");
        std::stringstream old("PLUTOFOLIAGE\t2\nCELL_SIZE\t16\n");
        Require(scene::ReadFoliageTypeAsset(old, loaded, &error) && loaded.interactionTags.empty(), "An old asset gained tags");

        const auto changed = scene::ChangedFoliageTypeProperties({}, asset);
        Require(std::find(changed.begin(), changed.end(), "InteractionTags") != changed.end(),
                "A tag change was not reported as an override");

        scene::Scene scene;
        auto &foliage = AddForest(scene, {});
        foliage.GetType(0)->asset.interactionTags = asset.interactionTags;
        scene::FoliageComponent restored;
        restored.Deserialize(foliage.Serialize());
        Require(restored.GetType(0) && restored.GetType(0)->asset.interactionTags == asset.interactionTags,
                "The tags did not survive scene serialization");
    }

    // Two types scattered over a transformed owner: "Pine" (tagged Chop) and "Bush" (tagged Gather).
    scene::FoliageComponent &AddMixedFoliage(scene::Scene &scene, const std::string &name, const glm::vec3 &offset, unsigned seed)
    {
        auto &foliage = AddForest(scene, "project://Pine.plutoprefab");
        foliage.GetOwner()->SetName(name);
        foliage.GetOwner()->SetLocalTransform(scene::Transform{.position = offset, .rotation = {0.0f, 30.0f, 0.0f}, .scale = glm::vec3(1.5f)});
        foliage.GetType(0)->asset.interactionTags = {"Chop"};
        auto &bush = foliage.AddType("Bush");
        bush.asset.interactionTags = {"Gather"};
        std::mt19937 random(seed);
        std::uniform_real_distribution<float> spread(-40.0f, 40.0f);
        std::uint64_t id = seed * 1000u;
        for (auto *type : {foliage.GetType(0), foliage.GetType(1)})
            for (int index = 0; index < 400; ++index)
                type->instances.push_back({.id = ++id, .position = {spread(random), 0.0f, spread(random)}});
        foliage.RestoreInstanceSnapshot(foliage.CaptureInstanceSnapshot());
        return foliage;
    }

    std::vector<std::uint64_t> BruteForce(const scene::FoliageComponent &foliage, const glm::vec3 &center, float radius,
                                          const std::string &tag)
    {
        std::vector<std::pair<float, std::uint64_t>> found;
        for (std::size_t typeIndex = 0; typeIndex < foliage.GetTypeCount(); ++typeIndex)
        {
            const auto &type = *foliage.GetType(typeIndex);
            if (!tag.empty() && !scene::HasFoliageInteractionTag(type.asset, tag)) continue;
            for (std::size_t index = 0; index < type.instances.size(); ++index)
            {
                const auto transform = foliage.GetInstanceWorldTransform({typeIndex, index});
                const float distance = glm::length(glm::vec3((*transform)[3]) - center);
                if (distance <= radius) found.emplace_back(distance, type.instances[index].id);
            }
        }
        std::sort(found.begin(), found.end());
        std::vector<std::uint64_t> ids;
        for (const auto &[distance, id] : found) ids.push_back(id);
        return ids;
    }

    std::vector<std::uint64_t> Ids(const scene::FoliageComponent &foliage, const std::vector<scene::FoliageInstanceLocation> &locations)
    {
        std::vector<std::uint64_t> ids;
        for (const auto &location : locations) ids.push_back(foliage.GetInstance(location)->id);
        return ids;
    }

    void TestRadiusQueries()
    {
        scene::Scene scene;
        auto &foliage = AddMixedFoliage(scene, "Wood", {100.0f, 2.0f, -30.0f}, 7);
        const auto gather = [](const scene::FoliageType &type) { return scene::HasFoliageInteractionTag(type.asset, "Gather"); };
        // Centres straddling grid cells, radii from under a cell to several, on a scaled and offset owner.
        for (const glm::vec3 center : {glm::vec3(100.0f, 2.0f, -30.0f), glm::vec3(112.3f, 2.5f, -17.9f), glm::vec3(76.0f, 2.0f, -71.0f)})
        {
            for (const float radius : {0.5f, 3.0f, 12.0f, 30.0f})
            {
                Require(Ids(foliage, foliage.FindInstancesInRadius(center, radius)) == BruteForce(foliage, center, radius, {}),
                        "The grid query disagreed with a full scan at radius " + std::to_string(radius));
                Require(Ids(foliage, foliage.FindInstancesInRadius(center, radius, gather)) == BruteForce(foliage, center, radius, "Gather"),
                        "The filtered query disagreed with a filtered scan at radius " + std::to_string(radius));
            }
        }
        const glm::vec3 center(100.0f, 2.0f, -30.0f);
        const auto all = BruteForce(foliage, center, 30.0f, "Gather");
        const auto nearest = Ids(foliage, foliage.FindInstancesInRadius(center, 30.0f, gather, 5));
        Require(all.size() > 5 && nearest == std::vector<std::uint64_t>(all.begin(), all.begin() + 5), "maxResults did not keep the nearest");

        // The grid follows instance changes: a removed instance is gone, a restored one is back.
        const std::array removedId{all.front()};
        const auto removed = foliage.RemoveInstances(removedId);
        Require(Ids(foliage, foliage.FindInstancesInRadius(center, 30.0f, gather)).front() == all[1], "A removed instance was still found");
        foliage.RestoreInstances(removed);
        Require(Ids(foliage, foliage.FindInstancesInRadius(center, 30.0f, gather)) == all, "A restored instance was not found again");
    }

    void TestSceneQuery()
    {
        scene::Scene scene;
        auto &near = AddMixedFoliage(scene, "Near", {0.0f, 0.0f, 0.0f}, 3);
        auto &far = AddMixedFoliage(scene, "Far", {50.0f, 0.0f, 0.0f}, 5);
        auto &hidden = AddMixedFoliage(scene, "Hidden", {0.0f, 0.0f, 0.0f}, 9);
        hidden.GetOwner()->SetActive(false);

        const glm::vec3 center(25.0f, 0.0f, 0.0f);
        const auto matches = scene::FindFoliageInstances(scene, {.center = center, .radius = 20.0f, .interactionTag = "Gather"});
        Require(!matches.empty(), "The scene query found nothing");
        bool sawNear = false, sawFar = false;
        for (std::size_t index = 0; index < matches.size(); ++index)
        {
            const auto &match = matches[index];
            Require(match.foliageEntity != hidden.GetOwner()->GetID(), "An inactive entity's foliage was found");
            Require(match.distance <= 20.0f && (index == 0 || matches[index - 1].distance <= match.distance), "Matches not nearest first");
            const auto *foliage = match.foliageEntity == near.GetOwner()->GetID() ? &near : &far;
            Require(scene::HasFoliageInteractionTag(foliage->GetType(match.typeIndex)->asset, "Gather"), "An untagged type was found");
            sawNear |= foliage == &near;
            sawFar |= foliage == &far;
        }
        Require(sawNear && sawFar, "The scene query did not search every active foliage component");
        Require(matches.size() == BruteForce(near, center, 20.0f, "Gather").size() + BruteForce(far, center, 20.0f, "Gather").size(),
                "The scene query missed instances");

        const auto three = scene::FindFoliageInstances(scene, {.center = center, .radius = 20.0f, .interactionTag = "Gather", .maxResults = 3});
        Require(three.size() == 3 && three[0].instanceId == matches[0].instanceId && three[2].instanceId == matches[2].instanceId,
                "maxResults did not keep the scene's nearest");
        // Only the trees have a proxy; the bushes are gathered through a proxy too once their type names one.
        Require(scene::FindFoliageInstances(scene, {.center = center, .radius = 20.0f, .interactionTag = "Gather", .requireProxy = true}).empty(),
                "requireProxy accepted a type without a proxy prefab");
        Require(!scene::FindFoliageInstances(scene, {.center = center, .radius = 20.0f, .requireProxy = true}).empty(),
                "requireProxy rejected the trees");
        Require(scene::FindFoliageInstances(scene, {.center = center, .radius = std::nanf("")}).empty(), "A NaN radius found instances");
    }

    void TestRuntimePhysicsContinuity()
    {
        const float blocked = SlidePast(false, false);
        const float cleared = SlidePast(true, false);
        Require(blocked < 3.0f, "The tree did not block the body: " + std::to_string(blocked));
        Require(cleared > 4.0f, "The removed tree still blocked the body: " + std::to_string(cleared));
        const float undisturbed = SlidePast(false, true);
        Require(std::abs(undisturbed - blocked) < 1e-4f,
                "Removing a distant tree disturbed the simulation: " + std::to_string(undisturbed) + " vs " + std::to_string(blocked));
    }
}

int main()
{
    try
    {
        TestAssetAndSceneRoundTrip();
        TestInteractionTags();
        TestRadiusQueries();
        TestSceneQuery();
        TestCellSignatures();
        TestPromoteAndRelease();
        TestProxyKeepsWind();
        TestRuntimePhysicsContinuity();
    }
    catch (const std::exception &exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
    std::cout << "Foliage proxy tests passed.\n";
    return 0;
}
