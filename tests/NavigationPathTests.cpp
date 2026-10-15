#include "PlutoGE/scene/NavigationSystem.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/NavAgentComponent.h"
#include "PlutoGE/scene/components/NavigationMeshComponent.h"
#include "PlutoGE/scene/components/RigidbodyComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include <iostream>
#include <stdexcept>
using namespace PlutoGE::scene;
void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
int main() try
{
    Scene scene;
    auto *floor = scene.AddEntity(std::make_unique<Entity>());
    floor->SetPosition({0, -.5f, 0});
    floor->CreateComponent<ColliderComponent>(ColliderComponentConfig{.size = {12, 1, 12}});
    NavigationBakeSettings settings; settings.boundsMin = {-4, -1, -4}; settings.boundsMax = {4, 4, 4}; settings.cellSize = 1;
    NavigationSystem navigation;
    {
        Scene taggedScene;
        auto *ground = taggedScene.AddEntity(std::make_unique<Entity>());
        ground->SetPosition({0, -.5f, 0});
        ground->AddTag("Ground");
        ground->CreateComponent<ColliderComponent>(ColliderComponentConfig{.size = {12, 1, 12}});
        auto *obstacle = taggedScene.AddEntity(std::make_unique<Entity>());
        obstacle->SetPosition({.5f, 1, .5f});
        obstacle->AddTag("Wall");
        obstacle->CreateComponent<ColliderComponent>(ColliderComponentConfig{.size = {1, 2, 3}});
        auto taggedSettings = settings;
        taggedSettings.staticObjectsOnly = true;
        const auto route = [&]() { return navigation.FindPath({-3.5f, 1, .5f}, {3.5f, 1, .5f}, .2f, 1.5f); };
        taggedSettings.includeTags = {"Ground"};
        Check(navigation.Bake(taggedScene, taggedSettings) && route().complete && route().points.size() == 2,
              "Include filter did not ignore an unmatched wall");
        taggedSettings.includeTags.push_back("Wall");
        Check(navigation.Bake(taggedScene, taggedSettings) && route().complete && route().points.size() > 2,
              "Include filter did not accept any matching tag");
        taggedSettings.excludeTags = {"Wall"};
        Check(navigation.Bake(taggedScene, taggedSettings) && route().complete && route().points.size() == 2,
              "Exclude tags did not override include tags");
        taggedSettings.includeTags.clear();
        Check(navigation.Bake(taggedScene, taggedSettings) && route().points.size() == 2,
              "Exclude-only filter failed");
        taggedSettings.includeTags = {"ground"};
        Check(!navigation.Bake(taggedScene, taggedSettings), "Tag matching was not case-sensitive");
        NavigationMeshComponent taggedMesh;
        taggedMesh.Deserialize({{"Include Tags", PropertyType::String, " Ground, Wall, , Ground "},
                                {"Exclude Tags", PropertyType::String, " Wall, Ignore "}});
        NavigationMeshComponent restored;
        restored.Deserialize(taggedMesh.Serialize());
        Check(restored.GetSettings().includeTags == std::vector<std::string>({"Ground", "Wall"}) &&
              restored.GetSettings().excludeTags == std::vector<std::string>({"Wall", "Ignore"}),
              "Tag fields did not trim, deduplicate or survive serialization");
    }
    for (int kind = 0; kind < 3; ++kind)
    {
        Scene filterScene;
        auto *ground = filterScene.AddEntity(std::make_unique<Entity>());
        ground->SetPosition({0, -.5f, 0});
        ground->CreateComponent<ColliderComponent>(ColliderComponentConfig{.size = {12, 1, 12}});
        auto *obstacle = filterScene.AddEntity(std::make_unique<Entity>());
        obstacle->SetPosition({.5f, 1, .5f});
        obstacle->CreateComponent<ColliderComponent>(ColliderComponentConfig{.size = {1, 2, 3}});
        if (kind < 2)
            obstacle->CreateComponent<RigidbodyComponent>(RigidbodyComponentConfig{.isKinematic = kind == 1});
        else
            obstacle->CreateComponent<MeshComponent>(MeshComponentConfig{})->SetStatic(false);
        auto filteredSettings = settings;
        filteredSettings.staticObjectsOnly = true;
        Check(navigation.Bake(filterScene, filteredSettings), "Static-only bake failed");
        Check(navigation.FindPath({-3.5f, 1, .5f}, {3.5f, 1, .5f}, .2f, 1.5f).points.size() == 2,
              "Static-only bake included a movable obstacle");
        filteredSettings.staticObjectsOnly = false;
        Check(navigation.Bake(filterScene, filteredSettings), "Unfiltered bake failed");
        Check(navigation.FindPath({-3.5f, 1, .5f}, {3.5f, 1, .5f}, .2f, 1.5f).points.size() > 2,
              "Unfiltered bake ignored a movable obstacle");
    }
    settings.staticObjectsOnly = true;
    NavigationMeshComponent serializedMesh(settings);
    NavigationMeshComponent restoredMesh;
    restoredMesh.Deserialize(serializedMesh.Serialize());
    bool restoredStaticOnly = false;
    for (const auto &property : restoredMesh.Serialize())
        if (property.name == "Static Objects Only") restoredStaticOnly = property.value == "true";
    Check(restoredStaticOnly, "Static-only setting did not survive serialization");
    for (float partitionX : {0.0f, 0.5f})
    {
        Scene partitionScene;
        auto *partitionFloor = partitionScene.AddEntity(std::make_unique<Entity>());
        partitionFloor->SetPosition({0, -.5f, 0});
        partitionFloor->CreateComponent<ColliderComponent>(ColliderComponentConfig{.size = {12, 1, 12}});
        auto *partition = partitionScene.AddEntity(std::make_unique<Entity>());
        partition->SetPosition({partitionX, 2, 0});
        partition->CreateComponent<ColliderComponent>(ColliderComponentConfig{.size = {.32f, 6, 3}});
        Check(navigation.Bake(partitionScene, settings), "Thin partition bake failed");
        const auto partitionRoute = navigation.FindPath({-3.5f, 1, .5f}, {3.5f, 1, .5f}, .2f, 1.5f);
        Check(partitionRoute.complete && partitionRoute.points.size() > 2,
              "Route crossed a thin partition extending above bake bounds");
    }
    Check(navigation.Bake(scene, settings), "Open-ground bake failed");
    const glm::vec3 start{-3.5f, 1, .5f}, end{3.5f, 1, .5f};
    const auto direct = navigation.FindPath(start, end, .2f, 1.5f);
    Check(direct.complete && direct.points.size() == 2 && direct.points.front() == start && direct.points.back() == end,
          "Direct route must retain actor-height endpoints");
    auto *wall = scene.AddEntity(std::make_unique<Entity>());
    wall->SetPosition({.5f, 1, .5f});
    wall->CreateComponent<ColliderComponent>(ColliderComponentConfig{.size = {1, 2, 3}});
    Check(navigation.Bake(scene, settings), "Obstacle bake failed");
    const auto detour = navigation.FindPath(start, end, .2f, 1.5f);
    Check(detour.complete && detour.points.size() > 2, "Path crossed an obstruction instead of taking a detour");
    const auto blockedEnd = navigation.FindPath(start, {.5f, 1, .5f}, .2f, 1.5f);
    Check(!blockedEnd.complete || (!blockedEnd.points.empty() && blockedEnd.points.back() != glm::vec3(.5f, 1, .5f)),
          "Projected destination was replaced with a point inside the wall");
    const auto outsideEnd = navigation.FindPath(start, {-4.2f, 1, .5f}, .2f, 1.5f);
    Check(outsideEnd.complete && outsideEnd.points.back().x >= settings.boundsMin.x,
          "Projected destination was replaced with a point outside the bake");
    Check(!navigation.FindPath({.5f, 1, .5f}, end, .2f, 1.5f).complete,
          "Start inside an obstruction produced an unchecked connection");
    const glm::vec3 nearby{-3.4f, 1, .6f};
    const auto shortPath = navigation.FindPath(start, nearby, .2f, 1.5f);
    Check(shortPath.complete && shortPath.points.size() == 2 && shortPath.points.front() == start && shortPath.points.back() == nearby,
          "Same-cell path lost its start or destination");
    auto barrier = std::make_unique<Entity>();
    barrier->SetPosition({.5f, 1, .5f});
    barrier->CreateComponent<ColliderComponent>(ColliderComponentConfig{.size = {1, 2, 12}});
    scene.AddEntity(std::move(barrier));
    Check(navigation.Bake(scene, settings), "Partitioned bake failed");
    Check(!navigation.FindPath(start, end, .2f, 1.5f).complete, "Unreachable destination reported reachable");
    auto *meshEntity = scene.AddEntity(std::make_unique<Entity>());
    auto *mesh = meshEntity->CreateComponent<NavigationMeshComponent>(settings);
    Check(mesh->Bake(), "Agent navigation bake failed");
    auto *bot = scene.AddEntity(std::make_unique<Entity>());
    bot->SetPosition(start);
    NavAgentConfig agentSettings;
    agentSettings.navigationMeshEntityId = meshEntity->GetID();
    agentSettings.agentRadius = .2f;
    agentSettings.agentHeight = 1.5f;
    auto *agent = bot->CreateComponent<NavAgentComponent>(agentSettings);
    Check(agent->SetDestination({-2.5f, 1, .5f}) && agent->HasPath(), "Reachable agent route failed");
    Check(!agent->SetDestination(end) && !agent->HasPath() && agent->GetPath().empty(),
          "Failed search created a straight-line fallback through a wall");
    // Match-sized stopping distances must not skip tight detour corners.
    for (const float dt : {1.0f / 60, 1.0f / 20, 1.0f / 10})
    {
        Scene cornerScene;
        auto *ground = cornerScene.AddEntity(std::make_unique<Entity>());
        ground->SetPosition({0, -.5f, 0});
        ground->CreateComponent<ColliderComponent>(ColliderComponentConfig{.size = {16, 1, 16}});
        auto *wall = cornerScene.AddEntity(std::make_unique<Entity>());
        wall->SetPosition({0, 1.5f, 0});
        wall->CreateComponent<ColliderComponent>(ColliderComponentConfig{.size = {1, 3, 6}});
        NavigationBakeSettings tightSettings;
        tightSettings.boundsMin = {-6, -1, -6}; tightSettings.boundsMax = {6, 5, 6};
        tightSettings.cellSize = .4f; tightSettings.staticObjectsOnly = true;
        auto *nav = cornerScene.AddEntity(std::make_unique<Entity>());
        auto *navMesh = nav->CreateComponent<NavigationMeshComponent>(tightSettings);
        Check(navMesh->Bake(), "Corner bake failed");
        auto *actor = cornerScene.AddEntity(std::make_unique<Entity>());
        actor->SetPosition({-3, 1, 0});
        ColliderComponentConfig capsule;
        capsule.shape = ColliderShape::Capsule; capsule.radius = .3f; capsule.height = 1.5f;
        actor->CreateComponent<ColliderComponent>(capsule);
        actor->CreateComponent<RigidbodyComponent>(RigidbodyComponentConfig{.useGravity = true, .isKinematic = true});
        auto config = agentSettings;
        config.navigationMeshEntityId = nav->GetID(); config.navigateOnStart = true;
        config.agentRadius = .5f; config.agentHeight = 2; config.stoppingDistance = .75f; config.speed = 4.8f;
        config.destination = {3, 1, 0};
        actor->CreateComponent<NavAgentComponent>(config);
        cornerScene.StartRuntime();
        for (int frame = 0; frame < static_cast<int>(20 / dt); ++frame) cornerScene.Update(dt);
        auto displacement = actor->GetWorldPosition() - config.destination; displacement.y = 0;
        Check(glm::length(displacement) < 1, "Agent stalled while following a tight wall detour");
        cornerScene.StopRuntime();
    }
    navigation.Clear();
    Check(!navigation.FindPath(start, end).complete, "Cleared navigation returned stale route");
    std::cout << "PASS: direct routes, actor heights, obstacle detours, unreachable goals and invalidation\n";
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
