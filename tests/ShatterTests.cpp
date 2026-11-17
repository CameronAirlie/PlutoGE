// Shatter and convex hull colliders without a GPU: a dynamic ConvexHull body built from runtime geometry falls and
// rests on a static box; Shatter turns a scaled, rotated cube (and a child mesh) into root entities with runtime
// geometry, the source's materials, ConvexHull colliders and dynamic rigidbodies that the next physics step picks up,
// placed exactly where the source was drawn, and leaves the source alone.
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Shatter.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/RigidbodyComponent.h"

#include <glm/glm.hpp>

#include <chrono>
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

    Entity *AddGround(Scene &scene)
    {
        auto *ground = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Ground"}));
        ground->SetPosition({0.0f, -0.5f, 0.0f});
        ground->CreateComponent<ColliderComponent>(ColliderComponentConfig{.size = {40.0f, 1.0f, 40.0f}});
        return ground;
    }

    void Step(Scene &scene, int frames)
    {
        for (int frame = 0; frame < frames; ++frame)
            scene.Update(1.0f / 60.0f);
    }

    void ConvexHullBodyFallsAndRests()
    {
        Scene scene;
        AddGround(scene);
        std::unique_ptr<render::Mesh> cube(render::Mesh::Cube());
        auto *body = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Hull"}));
        body->SetPosition({1.0f, 3.0f, -2.0f});
        auto *mesh = body->CreateComponent<MeshComponent>(MeshComponentConfig{});
        Require(mesh->SetRuntimeGeometry(cube->GetMeshData()), "Runtime geometry is accepted");
        body->CreateComponent<ColliderComponent>(ColliderComponentConfig{.shape = ColliderShape::ConvexHull});
        body->CreateComponent<RigidbodyComponent>(RigidbodyComponentConfig{.mass = 5.0f});

        scene.StartRuntime();
        Step(scene, 30);
        Require(body->GetWorldPosition().y < 2.9f, "The hull body falls");
        Step(scene, 210);
        const float height = body->GetWorldPosition().y;
        std::cout << "  convex hull cube rests at y = " << height << "\n";
        Require(std::abs(height - 0.5f) < 0.03f, "The hull cube comes to rest on the ground, half a metre up");
        scene.StopRuntime();
    }

    // A sphere has hundreds of points: its hull is reduced to at most 64, which must still roll to rest on its surface.
    void SimplifiedHullBodyRests()
    {
        Scene scene;
        AddGround(scene);
        std::unique_ptr<render::Mesh> sphere(render::Mesh::Sphere(24, 32));
        auto *body = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Ball"}));
        body->SetPosition({0.0f, 2.0f, 0.0f});
        body->SetScale(glm::vec3(2.0f));
        body->CreateComponent<MeshComponent>(MeshComponentConfig{})->SetRuntimeGeometry(sphere->GetMeshData());
        body->CreateComponent<ColliderComponent>(ColliderComponentConfig{.shape = ColliderShape::ConvexHull});
        body->CreateComponent<RigidbodyComponent>();
        scene.StartRuntime();
        Step(scene, 300);
        const float height = body->GetWorldPosition().y;
        std::cout << "  simplified hull of a 1 m ball rests at y = " << height << "\n";
        Require(height > 0.9f && height < 1.05f, "The scaled ball's simplified hull rests on its surface");
        scene.StopRuntime();
    }

    glm::vec3 WorldPoint(const Entity &entity, const render::MeshVertexData &vertex)
    {
        return glm::vec3(entity.GetWorldTransform() * glm::vec4(vertex.position[0], vertex.position[1], vertex.position[2], 1.0f));
    }

    void ShatterSpawnsPhysicsPieces()
    {
        Scene scene;
        AddGround(scene);
        std::unique_ptr<render::Mesh> cube(render::Mesh::Cube());
        render::Material rockMaterial;
        render::Material childMaterial;

        auto *rock = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Rock"}));
        rock->SetPosition({0.0f, 1.0f, 0.0f});
        rock->SetRotation({0.0f, 30.0f, 10.0f});
        rock->SetScale({2.0f, 1.0f, 1.0f});
        auto *rockMesh = rock->CreateComponent<MeshComponent>(MeshComponentConfig{.mesh = cube.get(), .material = &rockMaterial});
        rockMesh->SetMeshPositionOffset({0.0f, 0.25f, 0.0f});

        // A small child mesh: an eighth of the volume gets one whole piece.
        auto *pebble = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Pebble"}), rock);
        pebble->SetPosition({0.0f, 1.0f, 0.0f});
        pebble->SetScale({0.25f, 0.5f, 0.5f});
        pebble->CreateComponent<MeshComponent>(MeshComponentConfig{.mesh = cube.get(), .material = &childMaterial});

        // An invisible mesh is not broken.
        auto *hidden = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Hidden"}), rock);
        hidden->CreateComponent<MeshComponent>(MeshComponentConfig{.mesh = cube.get(), .material = &childMaterial})->SetVisible(false);

        scene.StartRuntime();
        Step(scene, 1);
        const glm::mat4 rockWorld = rock->GetWorldTransform() * rockMesh->GetMeshOffsetTransform();
        const glm::vec3 rockPosition = rock->GetWorldPosition();
        const std::size_t rootsBefore = scene.GetRootEntities().size();

        const auto start = std::chrono::steady_clock::now();
        const auto pieces = Shatter(scene, *rock, {.pieces = 6, .seed = 3, .minPieceSize = 0.0f, .tag = "Debris"});
        const double milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::cout << "  shattered into " << pieces.size() << " pieces in " << milliseconds << " ms\n";

        int rockPieces = 0;
        int pebblePieces = 0;
        double rockVolume = 0.0;
        for (auto *piece : pieces)
        {
            Require(piece && piece->GetParent() == nullptr && piece->GetScene() == &scene, "Pieces are root entities of the scene");
            Require(piece->HasTag("Debris"), "Pieces carry the tag");
            Require(glm::length(piece->GetWorldScale() - glm::vec3(1.0f)) < 1e-4f, "Pieces have unit scale");
            auto *mesh = piece->GetComponent<MeshComponent>();
            auto *collider = piece->GetComponent<ColliderComponent>();
            auto *body = piece->GetComponent<RigidbodyComponent>();
            Require(mesh && mesh->HasRuntimeGeometry() && mesh->GetMesh(), "Pieces draw runtime geometry");
            Require(!mesh->IsStatic(), "Pieces are not static");
            Require(collider && collider->GetShape() == ColliderShape::ConvexHull, "Pieces collide as convex hulls");
            Require(body && !body->IsKinematic() && body->GetMass() >= kMinShatterPieceMass, "Pieces are dynamic bodies");
            Require(glm::length(body->GetVelocity()) > 0.5f, "Pieces fly apart");

            const bool fromRock = piece->GetName().rfind("Rock piece ", 0) == 0;
            const bool fromPebble = piece->GetName().rfind("Pebble piece ", 0) == 0;
            Require(fromRock || fromPebble, "Pieces are named after their source: " + piece->GetName());
            Require(mesh->GetMaterialForSubmesh(0) == (fromRock ? &rockMaterial : &childMaterial), "Pieces keep their source's material");
            if (fromPebble)
            {
                ++pebblePieces;
                continue;
            }
            ++rockPieces;
            rockVolume += body->GetMass() / 2600.0;
            // Exactly where the source was drawn: every vertex inside the source's drawn box.
            const glm::mat4 toSource = glm::inverse(rockWorld);
            for (const auto &vertex : mesh->GetMesh()->GetMeshData().vertices)
            {
                const glm::vec3 local = glm::vec3(toSource * glm::vec4(WorldPoint(*piece, vertex), 1.0f));
                Require(glm::all(glm::lessThanEqual(glm::abs(local), glm::vec3(0.5005f))), "Piece vertices lie inside the source's drawn cube");
            }
        }
        Require(rockPieces >= 5 && rockPieces <= 6, "The rock breaks into about the pieces asked for");
        Require(pebblePieces == 1, "A mesh an eighth the size gets a single piece");
        Require(std::abs(rockVolume - 2.0) < 0.02, "The rock's pieces weigh its volume times the density");
        Require(scene.GetRootEntities().size() == rootsBefore + pieces.size(), "Every piece is added as a root");

        // The source is untouched.
        Require(rock->IsActive() && rockMesh->GetMesh() == cube.get() && !rockMesh->HasRuntimeGeometry(), "The source keeps its mesh");
        Require(glm::length(rock->GetWorldPosition() - rockPosition) < 1e-5f, "The source does not move");

        // The next step creates their bodies; they fall and settle on the ground.
        const auto before = scene.GetPhysicsSynchronizationStats();
        Step(scene, 1);
        const auto after = scene.GetPhysicsSynchronizationStats();
        Require(after.simulationBodiesCreated - before.simulationBodiesCreated >= pieces.size(), "The next step creates every piece's body");
        Step(scene, 300);
        for (auto *piece : pieces)
        {
            Require(piece->GetWorldPosition().y > -0.05f, "Pieces land on the ground: " + piece->GetName());
            Require(piece->GetWorldPosition().y < 2.5f, "Pieces fall: " + piece->GetName());
        }
        scene.StopRuntime();
    }

    void NothingToBreak()
    {
        Scene scene;
        auto *empty = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Empty"}));
        Require(Shatter(scene, *empty, {}).empty(), "An entity without meshes gives no pieces");
    }
}

int main()
{
    try
    {
        ConvexHullBodyFallsAndRests();
        SimplifiedHullBodyRests();
        ShatterSpawnsPhysicsPieces();
        NothingToBreak();
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAILED: " << error.what() << "\n";
        return 1;
    }
    std::cout << "PlutoGEShatterTests passed\n";
    return 0;
}
