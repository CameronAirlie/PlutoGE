#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/scene/components/FoliageComponent.h"

#include <iostream>
#include <memory>

int main()
{
    using namespace PlutoGE::scene;

    Scene scene;
    auto storage = std::make_unique<Entity>(EntityConfig{.name = "Forest"});
    auto *owner = scene.AddEntity(std::move(storage));
    auto *foliage = owner->CreateComponent<FoliageComponent>();
    auto &type = foliage->AddType("Tree");
    type.asset.cellSize = 16.0f;
    type.asset.collisionEnabled = true;
    type.asset.collisionCenter = {0, 1, 0};
    type.asset.collisionRadius = 0.5f;
    type.asset.collisionHeight = 2.0f;
    type.instances.push_back(FoliageInstance{.id = 1});

    foliage->SetSelectedTypeIndex(static_cast<int>(foliage->GetTypeCount() - 1));
    if (foliage->GetInstances().size() != 1 || foliage->GetInstances().front().id == 0)
    {
        std::cerr << "Foliage instance did not retain its stable ID.\n";
        return 1;
    }
    if (foliage->BuildCollisionCells().size() != 1)
    {
        std::cerr << "Foliage collision was not partitioned into one cell.\n";
        return 1;
    }

    PhysicsRaycastHit hit;
    if (!scene.Raycast({0, 1, 5}, {0, 0, -1}, 10, hit) || hit.entityId != owner->GetID() || hit.foliageInstanceId != 1)
    {
        std::cerr << "Raycast did not hit the foliage capsule.\n";
        return 1;
    }

    owner->SetPosition({10, 0, 0});
    if (!scene.Raycast({10, 1, 5}, {0, 0, -1}, 10, hit) || hit.entityId != owner->GetID())
    {
        std::cerr << "Foliage collision cache did not follow its owner transform.\n";
        return 1;
    }

    owner->SetScale({10, 5, 10});
    type.instances.front().position = {1, 0, 0};
    type.instances.front().scale = {2, 2, 2};
    foliage->SetSelectedTypeInstanceTransform(0, {1, 0, 0}, {0, 0, 0}, {2, 2, 2});
    const auto &scaledOwnerCells = foliage->BuildCollisionCells();
    if (scaledOwnerCells.empty() || scaledOwnerCells.front().instances.empty())
    {
        std::cerr << "Foliage collision was not rebuilt after scaling its owner.\n";
        return 1;
    }
    const glm::mat4 &instanceTransform = scaledOwnerCells.front().instances.front().worldTransform;
    if (glm::distance(glm::vec3(instanceTransform[3]), glm::vec3(20, 0, 0)) > 0.001f ||
        std::abs(glm::length(glm::vec3(instanceTransform[0])) - 1.0f) > 0.001f)
    {
        std::cerr << "Foliage collision inherited render or owner scale.\n";
        return 1;
    }

    type.instances.front().position = {2.0f, -4.0f, 3.0f};
    type.instances.front().rotationDegrees = {1.0f, 2.0f, 3.0f};
    type.instances.front().scale = {0.5f, 0.75f, 1.0f};
    const std::uint64_t revisionBeforeSelectedSnap = foliage->GetRevision();
    const std::size_t selectedSnapCount = foliage->SnapSelectedTypeInstancesToSurface(
        [](float x, float z)
        {
            return x + z;
        });
    if (selectedSnapCount != 1 || type.instances.front().position != glm::vec3(2.0f, 5.0f, 3.0f) ||
        type.instances.front().rotationDegrees != glm::vec3(1.0f, 2.0f, 3.0f) ||
        type.instances.front().scale != glm::vec3(0.5f, 0.75f, 1.0f) ||
        foliage->GetRevision() == revisionBeforeSelectedSnap)
    {
        std::cerr << "Selected foliage type did not snap vertically to the sampled surface.\n";
        return 1;
    }

    const std::size_t treeTypeIndex = static_cast<std::size_t>(foliage->GetSelectedTypeIndex());
    auto &secondType = foliage->AddType("Bush");
    secondType.instances.push_back(FoliageInstance{.id = 2, .position = {-2.0f, 10.0f, 1.0f}});
    const std::size_t allSnapCount = foliage->SnapAllInstancesToSurface(
        [](float x, float z)
        {
            return x * z;
        });
    const auto *snappedTreeType = foliage->GetType(treeTypeIndex);
    const auto *snappedBushType = foliage->GetSelectedType();
    if (allSnapCount != 2 || !snappedTreeType || !snappedBushType ||
        snappedTreeType->instances.front().position.y != 6.0f || snappedBushType->instances.front().position.y != -2.0f)
    {
        std::cerr << "All foliage types did not snap to the sampled surface.\n";
        return 1;
    }
    return 0;
}
