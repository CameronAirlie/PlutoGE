// Navigation region rebakes and time-sliced bakes: building a wall or opening a door re-samples only that box, and a
// large bake can be spread over frames while the previous one keeps answering queries.
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/NavigationSystem.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/NavigationMeshComponent.h"

#include <iostream>
#include <stdexcept>

using namespace PlutoGE::scene;

namespace
{
    void Check(bool value, const char *message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    Entity *AddBox(Scene &scene, const glm::vec3 &position, const glm::vec3 &size)
    {
        auto *entity = scene.AddEntity(std::make_unique<Entity>());
        entity->SetPosition(position);
        entity->CreateComponent<ColliderComponent>(ColliderComponentConfig{.size = size});
        scene.MarkColliderShapesDirty();
        return entity;
    }

    NavigationBakeSettings Settings()
    {
        NavigationBakeSettings settings;
        settings.boundsMin = {-6, -1, -6};
        settings.boundsMax = {6, 4, 6};
        settings.cellSize = 0.5f;
        return settings;
    }

    const glm::vec3 kStart{-5.0f, 0.0f, 0.25f};
    const glm::vec3 kEnd{5.0f, 0.0f, 0.25f};

    bool Direct(const NavigationSystem &navigation)
    {
        const auto path = navigation.FindPath(kStart, kEnd, 0.2f, 1.5f);
        return path.complete && path.points.size() == 2;
    }

    bool Detours(const NavigationSystem &navigation)
    {
        const auto path = navigation.FindPath(kStart, kEnd, 0.2f, 1.5f);
        return path.complete && path.points.size() > 2;
    }
}

int main()
try
{
    // Region rebake: a wall built after the bake is unknown until its box is re-sampled.
    {
        Scene scene;
        AddBox(scene, {0, -0.5f, 0}, {14, 1, 14});
        NavigationSystem navigation;
        Check(navigation.Bake(scene, Settings()) && Direct(navigation), "Open ground bakes a direct route");
        const auto openCells = navigation.GetDebugWalkablePoints().size();
        const auto bakedRevision = navigation.GetRevision();

        auto *wall = AddBox(scene, {0.25f, 1, 0}, {0.5f, 2, 6});
        Check(Direct(navigation), "A wall built after the bake is not seen until something is re-sampled");
        Check(navigation.RebakeRegion(scene, {0, 0, -3}, {0.5f, 2, 3}), "The wall's box is re-sampled");
        Check(navigation.GetRevision() != bakedRevision, "A region rebake changes the revision, so agents plan again");
        Check(Detours(navigation), "After the region rebake the route goes round the wall");
        NavigationSystem full;
        Check(full.Bake(scene, Settings()) && full.GetDebugWalkablePoints() == navigation.GetDebugWalkablePoints(),
              "A region rebake leaves the same walkable cells as a full bake with the wall");

        scene.RemoveEntity(wall);
        scene.MarkColliderShapesDirty();
        Check(navigation.RebakeRegion(scene, {0, 0, -3}, {0.5f, 2, 3}), "The cleared box is re-sampled");
        Check(Direct(navigation), "Taking the wall away opens the direct route again");
        Check(navigation.GetDebugWalkablePoints().size() == openCells,
              "A region rebake leaves exactly the cells a full bake would");

        Check(!navigation.RebakeRegion(scene, {50, 0, 50}, {60, 2, 60}), "A box outside the bake re-samples nothing");
        NavigationSystem empty;
        Check(!empty.RebakeRegion(scene, {0, 0, 0}, {1, 1, 1}), "Nothing to re-sample before a bake");
    }

    // Time-sliced bake: the old bake answers until the new one is complete.
    {
        Scene scene;
        AddBox(scene, {0, -0.5f, 0}, {14, 1, 14});
        NavigationSystem navigation;
        Check(navigation.Bake(scene, Settings()) && Direct(navigation), "First bake");
        AddBox(scene, {0.25f, 1, 0}, {0.5f, 2, 14});

        navigation.BeginBake(Settings());
        Check(navigation.IsBaking(), "A begun bake is in progress");
        const auto revision = navigation.GetRevision();
        int slices = 0;
        while (!navigation.ContinueBake(scene, 0.0))
        {
            ++slices;
            Check(Direct(navigation) && navigation.GetRevision() == revision, "The previous bake answers while baking");
            Check(slices < 1000, "A zero budget still advances a row per call");
        }
        Check(slices > 1, "A zero budget spreads the bake over several calls");
        Check(!navigation.IsBaking() && navigation.GetRevision() != revision, "The finished bake replaced the old one");
        Check(!navigation.FindPath(kStart, kEnd, 0.2f, 1.5f).complete, "The new bake sees the barrier");
        Check(navigation.ContinueBake(scene, 0.0), "Continuing with nothing to bake is done at once");
    }

    // The component: requested bakes honour the time slice; invalidated boxes are applied on update.
    {
        Scene scene;
        AddBox(scene, {0, -0.5f, 0}, {14, 1, 14});
        auto *meshEntity = scene.AddEntity(std::make_unique<Entity>());
        auto *mesh = meshEntity->CreateComponent<NavigationMeshComponent>(Settings());
        mesh->SetBakeTimeSlice(0.0001f);
        mesh->RequestBake();
        int updates = 0;
        while (mesh->IsBaking() && updates < 1000)
        {
            mesh->Update(0.016f);
            ++updates;
        }
        Check(updates > 1 && mesh->GetNavigation().IsBaked(), "A requested bake with a time slice completes over updates");
        Check(Direct(mesh->GetNavigation()), "The component's bake routes directly over open ground");

        AddBox(scene, {0.25f, 1, 0}, {0.5f, 2, 6});
        mesh->InvalidateRegion({0, 0, -3}, {0.5f, 2, 3});
        mesh->InvalidateRegion({0.1f, 0, -1}, {0.4f, 2, 1}); // overlapping: merged into the first
        mesh->Update(0.016f);
        Check(Detours(mesh->GetNavigation()), "An invalidated box is re-sampled on the next update");

        NavigationMeshComponent restored;
        restored.Deserialize(mesh->Serialize());
        Check(restored.GetBakeTimeSlice() > 0.0f, "The bake time slice survives serialization");
    }

    std::cout << "NavigationRegionTests passed\n";
    return 0;
}
catch (const std::exception &exception)
{
    std::cerr << "NavigationRegionTests failed: " << exception.what() << '\n';
    return 1;
}
