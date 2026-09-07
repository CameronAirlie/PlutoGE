#include "PlutoGE/ui/GroundPlacement.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/ColliderComponent.h"

#include <glm/geometric.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace PlutoGE::scene;
    using namespace PlutoGE::ui;

    void Require(bool value, const std::string &message)
    {
        if (!value) throw std::runtime_error(message);
    }

    Entity *Box(Scene &scene, glm::vec3 position, glm::vec3 size)
    {
        auto *entity = scene.AddEntity(std::make_unique<Entity>());
        entity->SetPosition(position);
        entity->CreateComponent<ColliderComponent>(ColliderComponentConfig{.size = size});
        return entity;
    }

    void Near(float value, float expected)
    {
        Require(std::abs(value - expected) < 0.002f, "Unexpected contact position");
    }

    void TestBoundsAndHierarchy()
    {
        Scene scene;
        Box(scene, {0, -0.5f, 0}, {100, 1, 100});
        auto *selected = Box(scene, {0, 10, 0}, {2, 2, 2});
        auto *child = Box(scene, {0, -3, 0}, {1, 2, 1});
        child->SetParent(selected);
        GroundPlacementOptions options;
        Transform result;
        std::string error;
        Require(GroundPlacement::Compute(scene, *selected, options, result, error), error);
        Near(result.position.y, 4);
        Near(selected->GetPosition().y, 10); // Planning must not mutate the scene.
        options.usePivot = true;
        Require(GroundPlacement::Compute(scene, *selected, options, result, error), error);
        Near(result.position.y, 0);
        options.surfaceOffset = 0.25f;
        Require(GroundPlacement::Compute(scene, *selected, options, result, error), error);
        Near(result.position.y, 0.25f);
        options.maxDistance = 1;
        const auto unchanged = result;
        Require(!GroundPlacement::Compute(scene, *selected, options, result, error), "No-hit case succeeded");
        Require(result.position == unchanged.position, "No-hit case changed the output");
    }

    void TestParentAndNormal()
    {
        Scene scene;
        auto *ground = Box(scene, {0, -0.5f, 0}, {100, 1, 100});
        auto *parent = scene.AddEntity(std::make_unique<Entity>());
        parent->SetPosition({0, 2, 0});
        parent->SetScale({2, 2, 2});
        auto *selected = Box(scene, {0, 5, 0}, {1, 2, 1});
        selected->SetParent(parent);
        Transform result;
        GroundPlacementOptions options;
        std::string error;
        Require(GroundPlacement::Compute(scene, *selected, options, result, error), error);
        Near(result.position.y, 0); // World center y=2 gives world bottom y=0.
        ground->SetRotation({0, 0, 25});
        options.alignToNormal = true;
        Require(GroundPlacement::Compute(scene, *selected, options, result, error), error);
        selected->SetPosition(result.position);
        selected->SetRotation(result.rotation);
        const auto up = glm::normalize(glm::vec3(selected->GetWorldTransform()[1]));
        const auto normal = glm::normalize(glm::vec3(ground->GetWorldTransform()[1]));
        Require(glm::dot(up, normal) > 0.999f, "Up axis did not align to the ground normal");
        options.randomYawDegrees = 90;
        options.minScaleFactor = 0.75f;
        options.maxScaleFactor = 1.25f;
        options.seed = 123;
        Transform repeated;
        Require(GroundPlacement::Compute(scene, *selected, options, result, error), error);
        Require(GroundPlacement::Compute(scene, *selected, options, repeated, error), error);
        Require(result.rotation == repeated.rotation && result.scale == repeated.scale, "Seeded placement is not reproducible");
        Require(result.scale.x >= 0.75f && result.scale.x <= 1.25f, "Scale exceeded its configured bounds");
        parent->SetScale({0, 1, 1});
        Require(!GroundPlacement::Compute(scene, *selected, options, result, error), "Singular parent was accepted");
    }
}

int main()
{
    try
    {
        TestBoundsAndHierarchy();
        TestParentAndNormal();
        std::cout << "PASS: ground contact, hierarchy exclusion, parents, normals and seeded placement\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
