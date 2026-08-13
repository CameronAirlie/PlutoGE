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
    return 0;
}
