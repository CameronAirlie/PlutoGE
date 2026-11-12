// Physics synchronization without a GPU: adding, removing and editing colliders changes only the bodies and shapes
// involved, in both the query world and the simulation world, and the bodies that survive still collide correctly.
// Shared geometry (meshes, terrain) is built once and reused across bodies, worlds and world rebuilds.
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/RigidbodyComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

using namespace PlutoGE;
using namespace PlutoGE::scene;

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    // Terrain: 32 x 32 m of flat ground at y = 0, centred on the origin.
    constexpr int kTerrainSamples = 33;

    std::string FlatSamples(int width, int depth, float height)
    {
        std::string text;
        for (int i = 0; i < width * depth; ++i)
            text += (i ? "," : "") + std::to_string(height);
        return text;
    }

    // A flat square of `cells` x `cells` quads, `size` metres across, from the origin along +x and +z. Each quad has
    // its own four vertices, as imported meshes split along UV seams do.
    render::MeshConfig GridMesh(int cells, float size)
    {
        render::MeshConfig config;
        const float step = size / static_cast<float>(cells);
        for (int z = 0; z < cells; ++z)
            for (int x = 0; x < cells; ++x)
            {
                const auto base = static_cast<unsigned int>(config.data.vertices.size());
                for (const auto [dx, dz] : {std::pair{0, 0}, std::pair{1, 0}, std::pair{0, 1}, std::pair{1, 1}})
                    config.data.vertices.push_back(render::MeshVertexData{
                        .position = {static_cast<float>(x + dx) * step, 0.0f, static_cast<float>(z + dz) * step},
                        .normal = {0.0f, 1.0f, 0.0f},
                        .uv = {0.0f, 0.0f},
                        .tangent = {1.0f, 0.0f, 0.0f, 1.0f},
                    });
                for (const unsigned int index : {0u, 2u, 1u, 1u, 2u, 3u})
                    config.data.indices.push_back(base + index);
            }
        return config;
    }

    struct PhysicsScene
    {
        Scene scene;
        render::Mesh rock{GridMesh(40, 4.0f)}; // 3200 triangles
        TerrainComponent *terrain = nullptr;

        PhysicsScene()
        {
            auto *entity = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Terrain"}));
            entity->SetPosition({-16.0f, 0.0f, -16.0f});
            terrain = entity->CreateComponent<TerrainComponent>(
                TerrainComponentConfig{.width = kTerrainSamples, .depth = kTerrainSamples, .cellSize = 1.0f});
            terrain->Deserialize({{"HeightSamples", PropertyType::String, FlatSamples(kTerrainSamples, kTerrainSamples, 0.0f)}});
            entity->CreateComponent<ColliderComponent>(ColliderComponentConfig{.shape = ColliderShape::Terrain});
        }

        Entity *AddRock(const glm::vec3 &position, float scale = 1.0f)
        {
            auto *entity = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Rock"}));
            entity->SetPosition(position);
            entity->SetScale(glm::vec3(scale));
            entity->CreateComponent<MeshComponent>(MeshComponentConfig{.mesh = &rock});
            entity->CreateComponent<ColliderComponent>(ColliderComponentConfig{.shape = ColliderShape::Mesh});
            return entity;
        }

        Entity *AddBox(const glm::vec3 &position, bool dynamic = false)
        {
            auto *entity = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Box"}));
            entity->SetPosition(position);
            entity->CreateComponent<ColliderComponent>();
            if (dynamic)
                entity->CreateComponent<RigidbodyComponent>();
            return entity;
        }

        // Synchronizes the query world and returns what that cost.
        PhysicsSynchronizationStats Sync()
        {
            const auto before = scene.GetPhysicsSynchronizationStats();
            scene.SynchronizePhysicsQueries();
            return Difference(before, scene.GetPhysicsSynchronizationStats());
        }

        // Runs one runtime update and returns what that cost.
        PhysicsSynchronizationStats Step()
        {
            const auto before = scene.GetPhysicsSynchronizationStats();
            scene.Update(1.0f / 60.0f);
            return Difference(before, scene.GetPhysicsSynchronizationStats());
        }

        // The height a downward ray from high above (x, z) hits, or NaN when it hits nothing.
        float GroundHeight(float x, float z) const
        {
            PhysicsRaycastHit hit;
            if (!scene.Raycast({x, 50.0f, z}, {0.0f, -1.0f, 0.0f}, 100.0f, hit))
                return std::nanf("");
            return hit.point.y;
        }

        static PhysicsSynchronizationStats Difference(const PhysicsSynchronizationStats &before,
                                                      const PhysicsSynchronizationStats &after)
        {
            return {
                .queryBodiesCreated = after.queryBodiesCreated - before.queryBodiesCreated,
                .simulationBodiesCreated = after.simulationBodiesCreated - before.simulationBodiesCreated,
                .simulationWorldRebuilds = after.simulationWorldRebuilds - before.simulationWorldRebuilds,
                .collisionShapesBuilt = after.collisionShapesBuilt - before.collisionShapesBuilt,
            };
        }
    };

    bool Near(float value, float expected, float tolerance = 0.02f)
    {
        return std::abs(value - expected) <= tolerance;
    }

    void QueryMembershipIsIncremental()
    {
        PhysicsScene world;
        world.AddRock({8.0f, 2.0f, 8.0f});
        auto initial = world.Sync();
        Require(initial.queryBodiesCreated == 2 && initial.collisionShapesBuilt == 2, "The first sync builds terrain and rock");
        Require(Near(world.GroundHeight(9.0f, 9.0f), 2.0f), "A ray onto the rock hits its surface");
        Require(Near(world.GroundHeight(0.0f, 0.0f), 0.0f), "A ray onto open ground hits the terrain");

        auto *box = world.AddBox({0.0f, 5.0f, 0.0f});
        auto added = world.Sync();
        Require(added.queryBodiesCreated == 1 && added.collisionShapesBuilt == 1,
                "Adding a collider creates only its own body and shape");
        Require(Near(world.GroundHeight(0.0f, 0.0f), 5.5f), "The added box is hit");

        world.scene.RemoveEntity(box);
        auto removed = world.Sync();
        Require(removed.queryBodiesCreated == 0 && removed.collisionShapesBuilt == 0, "Removing a collider creates nothing");
        Require(Near(world.GroundHeight(0.0f, 0.0f), 0.0f), "The removed box is no longer hit");
        Require(Near(world.GroundHeight(9.0f, 9.0f), 2.0f), "The rock survives another collider's removal");

        auto unchanged = world.Sync();
        Require(unchanged.queryBodiesCreated == 0 && unchanged.collisionShapesBuilt == 0, "An unchanged scene costs nothing");
    }

    void MeshGeometryIsShared()
    {
        PhysicsScene world;
        world.Sync();
        world.AddRock({8.0f, 2.0f, 8.0f});
        Require(world.Sync().collisionShapesBuilt == 1, "The first rock builds its mesh shape");
        world.AddRock({-12.0f, 3.0f, 8.0f});
        auto second = world.Sync();
        Require(second.queryBodiesCreated == 1 && second.collisionShapesBuilt == 0,
                "A second rock with the same mesh and scale shares the first one's shape");
        world.AddRock({-12.0f, 1.0f, -12.0f}, 2.0f);
        Require(world.Sync().collisionShapesBuilt == 1, "A rock at another scale gets a shape of its own");

        Require(Near(world.GroundHeight(9.0f, 9.0f), 2.0f), "The first rock is hit");
        Require(Near(world.GroundHeight(-11.0f, 9.0f), 3.0f), "The shared shape is placed at the second rock");
        Require(Near(world.GroundHeight(-6.5f, -6.5f), 1.0f), "The scaled rock spans its scaled size");
    }

    void HoleEditRebuildsOnlyTheTerrain()
    {
        PhysicsScene world;
        world.AddRock({8.0f, 2.0f, 8.0f});
        world.Sync();
        Require(Near(world.GroundHeight(4.5f, 4.5f), 0.0f), "Solid ground before the hole");

        world.terrain->SetHole(20, 20, true); // world x and z 4..5
        auto edited = world.Sync();
        Require(edited.queryBodiesCreated == 2, "A shape edit recreates the collider bodies");
        Require(edited.collisionShapesBuilt == 1, "Only the edited terrain is rebuilt; the rock comes from the cache");
        Require(std::isnan(world.GroundHeight(4.5f, 4.5f)), "A ray through the new hole hits nothing");
        Require(Near(world.GroundHeight(3.5f, 4.5f), 0.0f), "The ground beside the hole is still solid");
        Require(Near(world.GroundHeight(9.0f, 9.0f), 2.0f), "The rock is still hit");
    }

    void SimulationSpawnsAreIncremental()
    {
        PhysicsScene world;
        world.AddRock({8.0f, 2.0f, 8.0f});
        auto *resting = world.AddBox({-4.0f, 1.0f, -4.0f}, true);
        world.scene.StartRuntime();
        auto started = world.Step();
        Require(started.simulationWorldRebuilds == 1 && started.simulationBodiesCreated == 3,
                "Starting the runtime builds the simulation world");

        auto *spawned = world.AddBox({9.0f, 4.0f, 9.0f}, true);
        auto spawn = world.Step();
        Require(spawn.simulationWorldRebuilds == 0 && spawn.simulationBodiesCreated == 1,
                "A spawn adds one body without rebuilding the world");
        Require(spawn.collisionShapesBuilt <= 2, "A spawn builds only the new box's shapes (one per world at most)");
        for (int frame = 0; frame < 180; ++frame)
            world.Step();
        Require(Near(spawned->GetWorldPosition().y, 2.5f, 0.1f), "The spawned box comes to rest on the rock's mesh shape");
        Require(Near(resting->GetWorldPosition().y, 0.5f, 0.1f), "The first box rests on the terrain");

        world.scene.DestroyEntity(spawned->GetID());
        auto destroyed = world.Step();
        Require(destroyed.simulationWorldRebuilds == 0 && destroyed.simulationBodiesCreated == 0,
                "Destroying a body removes it without rebuilding the world");
        Require(world.Step().simulationBodiesCreated == 0, "A removal does not repeat");

        world.scene.SynchronizePhysicsQueries(); // the query world keeps terrain and rock shapes alive
        world.scene.StopRuntime();
        world.scene.StartRuntime();
        auto restarted = world.Step();
        Require(restarted.simulationWorldRebuilds == 1, "Restarting the runtime rebuilds the simulation world");
        Require(restarted.collisionShapesBuilt == 1,
                "A world rebuild reuses terrain and mesh shapes; only the box's primitive shape is built");
        world.scene.StopRuntime();
    }
}

int main()
{
    try
    {
        QueryMembershipIsIncremental();
        MeshGeometryIsShared();
        HoleEditRebuildsOnlyTheTerrain();
        SimulationSpawnsAreIncremental();
    }
    catch (const std::exception &error)
    {
        std::cerr << "PhysicsSynchronizationTests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "PhysicsSynchronizationTests passed\n";
    return 0;
}
