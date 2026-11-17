#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scripting/ScriptEngine.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

using namespace PlutoGE;

// A C# mining script shatters a rock through PlutoGE.ScriptCore's Fracture API and sets ColliderShape.ConvexHull;
// the request crosses the managed bridge intact and the pieces appear in the native scene.
namespace
{
    void Require(bool value, const std::string &message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    template <typename T>
    T Field(const std::unordered_map<std::string, scripting::ScriptFieldValue> &values, const char *name)
    {
        const auto found = values.find(name);
        Require(found != values.end(), std::string("Probe field missing: ") + name);
        return std::get<T>(found->second);
    }
}

int main()
try
{
    auto &engine = core::Engine::GetInstance();
    auto &scripts = engine.GetScriptEngine();
    struct Reset
    {
        ~Reset()
        {
            core::Engine::GetInstance().SetScene(nullptr);
            core::Engine::GetInstance().GetScriptEngine().Shutdown();
        }
    } reset;

    std::unique_ptr<render::Mesh> cube(render::Mesh::Cube());
    render::Material material;
    scene::Scene scene;
    auto *rock = scene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Rock"}));
    rock->SetPosition({3.0f, 1.0f, 0.0f});
    rock->CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{.mesh = cube.get(), .material = &material});
    auto *collider = rock->CreateComponent<scene::ColliderComponent>(scene::ColliderComponentConfig{.shape = scene::ColliderShape::Mesh});
    auto *owner = scene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Miner"}));
    const auto rootsBefore = scene.GetRootEntities().size();

    engine.SetScene(&scene);
    scripts.Initialize();
    Require(scripts.LoadAssembly(PLUTOGE_FRACTURE_PROBE_ASSEMBLY), scripts.GetLastError());
    auto instance = scripts.CreateInstance("PlutoGE.FractureProbe.FractureProbe");
    Require(instance != nullptr, scripts.GetLastError());
    instance->SetOwner(owner);
    instance->OnCreate();

    const auto values = instance->GetFieldValuesSnapshot();
    Require(Field<std::string>(values, "Failure").empty(), "Probe failed: " + Field<std::string>(values, "Failure"));
    Require(Field<bool>(values, "Supported"), "Fracture.IsSupported is false on this host");
    const int pieces = Field<int>(values, "PieceCount");
    Require(pieces >= 5 && pieces <= 6, "The script got " + std::to_string(pieces) + " pieces");
    Require(scene.GetRootEntities().size() == rootsBefore + static_cast<std::size_t>(pieces), "The pieces are root entities of the scene");
    Require(Field<bool>(values, "PiecesAreHulls"), "Pieces are not tagged ConvexHull bodies as seen from C#");
    Require(std::abs(Field<float>(values, "TotalMass") - 1000.0f) < 10.0f, "The density did not cross the bridge");
    Require(Field<bool>(values, "ShapeRoundTrips"), "ColliderShape.ConvexHull and Mesh do not round-trip");
    Require(collider->GetShape() == scene::ColliderShape::Mesh, "The scripted shape did not reach the native collider");
    Require(rock->IsActive() && rock->GetComponent<scene::MeshComponent>()->GetMesh() == cube.get(), "Shatter left the rock alone");
    std::cout << "Fracture scripting: the probe shattered the rock into " << pieces << " pieces\n";
    return 0;
}
catch (const std::exception &exception)
{
    std::cerr << exception.what() << '\n';
    return 1;
}
