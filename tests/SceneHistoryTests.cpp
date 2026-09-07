#include "PlutoGE/ui/SceneHistory.h"
#include "PlutoGE/ui/SceneSnapshots.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/assets/Project.h"
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <filesystem>

namespace
{
    void Require(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
    std::string Snapshot(const PlutoGE::scene::Scene &scene)
    {
        std::string state, error;
        Require(PlutoGE::scene::SceneSerializer::SaveToString(scene, state, &error), "Snapshot failed");
        return state;
    }
    void TestBuiltinMeshRestoration()
    {
        using namespace PlutoGE::scene;
        using PlutoGE::assets::Project;
        const std::string references[] = {
            std::string(Project::kBuiltinCubeMeshReference), std::string(Project::kBuiltinSphereMeshReference),
            std::string(Project::kBuiltinPlaneMeshReference), std::string(Project::kBuiltinCylinderMeshReference),
            std::string(Project::kBuiltinQuadMeshReference)};
        auto &assets = PlutoGE::core::Engine::GetInstance().GetAssetManager();
        Scene scene;
        for (const auto &reference : references)
        {
            auto *entity = scene.AddEntity(std::make_unique<Entity>());
            auto *mesh = entity->CreateComponent<MeshComponent>(MeshComponentConfig{});
            mesh->Deserialize({{"MeshAssetReference", PropertyType::String, reference}});
            Require(mesh->GetMesh() == assets.LoadMeshAsset(reference), "Direct built-in reference was not loaded");
            Require(mesh->GetMeshAssetReference() == reference, "Built-in reference was not retained");
            MeshComponent legacy(MeshComponentConfig{});
            legacy.Deserialize({{"SourceMeshPath", PropertyType::String, reference}});
            Require(legacy.GetMesh() == mesh->GetMesh(), "Legacy SourceMeshPath was not restored");
            MeshComponent modelIdentity(MeshComponentConfig{});
            modelIdentity.Deserialize({{"ModelAssetId", PropertyType::String, reference}});
            Require(modelIdentity.GetMesh() == mesh->GetMesh(), "Existing model-identity loading regressed");
        }
        struct TemporaryScene
        {
            std::filesystem::path path = std::filesystem::temp_directory_path() /
                ("PlutoGE-builtin-roundtrip-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".plutoscene");
            ~TemporaryScene() { std::error_code ignored; std::filesystem::remove(path, ignored); }
        } file;
        std::string error;
        Require(SceneSerializer::Save(scene, file.path.string(), &error), "Could not save primitive scene");
        auto loaded = SceneSerializer::Load(file.path.string(), &error);
        Require(loaded && error.empty(), "Could not load primitive scene");
        const auto beforePlay = Snapshot(*loaded);
        for (int cycle = 0; cycle < 3; ++cycle)
        {
            for (auto *entity : loaded->GetRootEntities())
                entity->GetComponent<MeshComponent>()->SetMesh(nullptr); // Runtime-only change.
            loaded = PlutoGE::ui::LoadSceneSnapshot(beforePlay, error);
            Require(loaded && error.empty(), "Pre-play snapshot restore failed");
            const auto entities = loaded->GetRootEntities();
            Require(entities.size() == std::size(references), "Primitive count changed");
            for (auto *entity : entities)
            {
                auto *mesh = entity->GetComponent<MeshComponent>();
                Require(mesh && mesh->GetMesh() && mesh->GetMesh()->GetIndexCount() > 0, "Restore lost primitive geometry");
                Require(mesh->GetMesh() == assets.LoadMeshAsset(mesh->GetMeshAssetReference()), "Restore lost mesh identity");
            }
            Require(Snapshot(*loaded) == beforePlay, "Repeated restore changed primitive serialization");
        }
    }

}
int main()
{
    try
    {
        using namespace PlutoGE::ui;
        using namespace PlutoGE::scene;
        TestBuiltinMeshRestoration();
        auto scene = std::make_unique<Scene>();
        auto *entity = scene->AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Before"}));
        const auto id = entity->GetID();
        entity->CreateComponent<LightComponent>()->SetIntensity(2);
        auto &assets = PlutoGE::core::Engine::GetInstance().GetAssetManager();
        const std::string cubeReference(PlutoGE::assets::Project::kBuiltinCubeMeshReference);
        auto *cube = assets.LoadMeshAsset(cubeReference);
        Require(cube && cube->GetIndexCount() > 0, "Built-in cube creation failed");
        auto *mesh = entity->CreateComponent<MeshComponent>(MeshComponentConfig{.mesh = cube, .material = assets.LoadMaterialAsset(std::string(PlutoGE::assets::Project::kBuiltinDefaultShadedMaterialReference))});
        mesh->SetMeshAssetReference(cubeReference);
        Require(mesh->GetModelAssetId().empty(), "Cube should not require an imported model identity");
        const auto before = Snapshot(*scene);
        entity->SetName("After");
        entity->SetPosition({3, 4, 5});
        entity->GetComponent<LightComponent>()->SetIntensity(8);
        const auto after = Snapshot(*scene);
        std::vector<SceneHistoryEntry> undo{{.label = "Inspector", .beforeState = before, .afterState = after}}, redo;
        const auto restore = [&](const std::string &state) {
            std::string error;
            auto staged = LoadSceneSnapshot(state, error);
            if (!staged) return false;
            auto *restoredMesh = staged->FindEntityByID(id)->GetComponent<MeshComponent>();
            Require(restoredMesh && restoredMesh->GetMesh() == cube, "Snapshot restore lost the built-in cube mesh");
            Require(restoredMesh->GetMeshAssetReference() == cubeReference, "Snapshot restore lost the mesh reference");
            scene = std::move(staged);
            return true;
        };
        Require(!TransferSceneHistory(undo, redo, [](const auto &) { return false; }), "Failed application accepted");
        Require(undo.size() == 1 && redo.empty() && Snapshot(*scene) == after, "Failure lost history or changed scene");
        Require(TransferSceneHistory(undo, redo, [&](const auto &entry) { return restore(entry.beforeState); }), "Undo failed");
        Require(undo.empty() && redo.size() == 1 && Snapshot(*scene) == before, "Undo did not restore saved state");
        Require(scene->FindEntityByID(id)->GetName() == "Before", "Owner ID/name did not survive restore");
        Require(TransferSceneHistory(redo, undo, [&](const auto &entry) { return restore(entry.afterState); }), "Redo failed");
        Require(Snapshot(*scene) == after && scene->FindEntityByID(id)->GetComponent<LightComponent>()->GetLight().intensity == 8, "Component edit did not roundtrip");
        undo.push_back({.label = "Bad snapshot", .beforeState = "broken"});
        Require(!TransferSceneHistory(undo, redo, [&](const auto &entry) { return restore(entry.beforeState); }) && undo.size() == 2, "Malformed restore lost entry");
        Require(Snapshot(*scene) == after, "Malformed restore replaced scene");
        undo.pop_back();
        int value = 2;
        undo.push_back({.label = "Command", .undo = [&] { value = 1; return true; }, .redo = [&] { value = 2; return true; }});
        Require(TransferSceneHistory(undo, redo, [](const auto &entry) { return entry.undo(); }) && value == 1, "Command undo failed");
        Require(TransferSceneHistory(redo, undo, [](const auto &entry) { return entry.redo(); }) && value == 2, "Command redo failed");
        std::cout << "PASS: history transfer, failed restores, serialized inspector edits and command history\n";
        return 0;
    }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
