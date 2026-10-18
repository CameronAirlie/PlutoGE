#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include <iostream>
#include <limits>
#include <stdexcept>

int main() try
{
    using namespace PlutoGE::scene;
    Scene scene;
    auto *wall = scene.AddEntity(std::make_unique<Entity>());
    wall->SetPosition({0, 1, -2});
    wall->CreateComponent<ColliderComponent>(ColliderComponentConfig{.size = {4, 2, 1}});
    PhysicsRaycastHit hit;
    auto query = [&](glm::vec3 center, glm::vec3 delta, EntityID ignored = 0)
    { return scene.QueryCapsule(center, delta, .35f, 1.8f, hit, ignored); };
    auto check = [](bool condition, const char *message) { if (!condition) throw std::runtime_error(message); };
    check(!query({0, 1, 0}, {}), "Empty capsule reported occupied");
    check(query({0, 1, -2}, {}), "Initial penetration missed");
    check(query({0, 1, 0}, {0, 0, -3}) && hit.entityId == wall->GetID() &&
          hit.distance > 1.1f && hit.distance < 1.2f && hit.normal.z > .9f, "Capsule sweep missed wall or wrong distance/normal");
    check(!query({0, 1, 0}, {0, 0, -3}, wall->GetID()), "Ignored entity included");
    wall->GetComponent<ColliderComponent>()->SetTrigger(true);
    scene.Update(0);
    check(!query({0, 1, 0}, {0, 0, -3}), "Trigger blocked sweep");
    check(!query({0, 1, -2}, {}), "Trigger blocked overlap");
    wall->GetComponent<ColliderComponent>()->SetTrigger(false);
    scene.Update(0);
    check(query({0, 1, -2}, {0, 2, 0}) && hit.distance == 0, "Sweeping out of penetration lost initial overlap");
    check(!scene.QueryCapsule({0, 1, 0}, {}, -1, 1.8f, hit), "Negative radius accepted");
    check(!scene.QueryCapsule({0, 1, 0}, {}, .5f, .2f, hit), "Height below diameter accepted");
    check(!scene.QueryCapsule({std::numeric_limits<float>::quiet_NaN(), 1, 0}, {}, .35f, 1.8f, hit), "NaN position accepted");
    check(!query({3, 1, 0}, {0, 0, -3}), "Nearby wall incorrectly blocks clear route");
    std::cout << "Capsule overlap/sweep checks passed\n";
    return 0;
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
