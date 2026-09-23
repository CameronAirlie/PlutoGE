#include "PlutoGE/scene/NavigationSystem.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
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
    auto barrier = std::make_unique<Entity>();
    barrier->SetPosition({.5f, 1, .5f});
    barrier->CreateComponent<ColliderComponent>(ColliderComponentConfig{.size = {1, 2, 12}});
    scene.AddEntity(std::move(barrier));
    Check(navigation.Bake(scene, settings), "Partitioned bake failed");
    Check(!navigation.FindPath(start, end, .2f, 1.5f).complete, "Unreachable destination reported reachable");
    navigation.Clear();
    Check(!navigation.FindPath(start, end).complete, "Cleared navigation returned stale route");
    std::cout << "PASS: direct routes, actor heights, obstacle detours, unreachable goals and invalidation\n";
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
