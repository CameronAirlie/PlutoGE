// Foliage proxies (FoliageProxies.h) and the incremental foliage physics they rely on: promoting an instance swaps it
// for a prefab entity at the same transform, releasing puts it back, and neither disturbs the rest of the physics world.
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/platform/ContentPack.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/FoliageProxies.h"
#include "PlutoGE/scene/FoliageTypeAsset.h"
#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/FoliageProxyComponent.h"
#include "PlutoGE/scene/components/RigidbodyComponent.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
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
        TestCellSignatures();
        TestPromoteAndRelease();
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
