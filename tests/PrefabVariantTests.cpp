#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/assets/AssetDatabase.h"
#include "PlutoGE/assets/ProjectValidation.h"
#include "PlutoGE/assets/AssetReferences.h"
#include "PlutoGE/ui/SceneSnapshots.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace PlutoGE;
void Require(bool value, const std::string &message) { if (!value) throw std::runtime_error(message); }
struct Scratch
{
    std::filesystem::path root = std::filesystem::temp_directory_path() / ("PlutoGE-variants-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~Scratch() { if (root.parent_path() == std::filesystem::temp_directory_path() && root.filename().string().starts_with("PlutoGE-variants-")) { std::error_code ec; std::filesystem::remove_all(root, ec); } }
};
int main()
{
    try
    {
        Scratch scratch;
        auto assetsPath = scratch.root / "Assets";
        core::Engine::GetInstance().GetAssetManager().SetProjectContext(scratch.root.string());
        scene::Scene authoring, instances;
        auto *base = authoring.AddEntity(std::make_unique<scene::Entity>());
        base->SetName("Base");
        base->CreateComponent<scene::ColliderComponent>();
        auto *child = authoring.AddEntity(std::make_unique<scene::Entity>(), base);
        child->SetName("Inherited child");
        std::string error;
        Require(scene::Prefab::SaveFromEntity(*base, assetsPath / "Base.plutoprefab", &error), error);
        auto *instance = scene::Prefab::Instantiate(instances, "project://Base.plutoprefab", nullptr, &error);
        Require(instance != nullptr, error);
        instance->SetName("Variant name"); instance->AddPrefabOverride("Name");
        instance->GetComponent<scene::ColliderComponent>()->SetRadius(2);
        instance->AddPrefabOverride("Component:ColliderComponent:Radius");
        instance->GetComponent<scene::ColliderComponent>()->SetSurfaceAssetReference("project://Stone.plutosurface");
        instance->AddPrefabOverride("Component:ColliderComponent:Surface Asset");
        std::ofstream(assetsPath / "Stone.plutosurface") << "SurfaceResponseVersion=1\n";
        Require(scene::Prefab::SaveVariant(*instance, assetsPath / "Variant.plutoprefab", &error), error);
        auto *variant = scene::Prefab::Instantiate(instances, "project://Variant.plutoprefab", nullptr, &error);
        Require(variant && variant->GetName() == "Variant name" && variant->GetComponent<scene::ColliderComponent>()->GetRadius() == 2, error);
        variant->SetScale({3,3,3}); variant->AddPrefabOverride("Transform.Scale");
        Require(scene::Prefab::SaveVariant(*variant, assetsPath / "Nested.plutoprefab", &error), error);
        auto *nested = scene::Prefab::Instantiate(instances, "project://Nested.plutoprefab", nullptr, &error);
        Require(nested && nested->GetScale().x == 3 && nested->GetName() == "Variant name", error);
        base->SetPosition({4,5,6}); child->SetName("Changed child");
        Require(scene::Prefab::SaveFromEntity(*base, assetsPath / "Base.plutoprefab", &error), error);
        Require(!scene::Prefab::IsReady("project://Nested.plutoprefab"), "Base change did not invalidate nested cache");
        Require(scene::Prefab::UpdateInstances(instances, "project://Base.plutoprefab", &error) == 3, error);
        Require(nested->GetPosition().x == 4 && nested->GetScale().x == 3 && nested->GetChildren()[0]->GetName() == "Changed child", "Inheritance failed");
        variant->SetName("Applied"); variant->AddPrefabOverride("Name");
        Require(scene::Prefab::ApplyInstanceToPrefab(*variant, &error), error);
        Require(scene::Prefab::GetVariantBase("project://Variant.plutoprefab") == "project://Base.plutoprefab", "Apply flattened variant");
        Require(scene::Prefab::UpdateInstance(*nested, &error) && nested->GetName() == "Applied", error);
        nested->SetName("Local"); nested->AddPrefabOverride("Name");
        std::string snapshot;
        Require(scene::SceneSerializer::SaveToString(instances, snapshot, &error), error);
        Require(scene::Prefab::RevertInstance(*nested, &error) && nested->GetName() == "Applied" && nested->GetPrefabOverrides().empty(), error);
        auto restored = ui::LoadSceneSnapshot(snapshot, error);
        Require(restored && restored->FindEntityByID(nested->GetID())->GetName() == "Local", "Undo snapshot failed");
        std::ofstream(assetsPath / "Cycle.plutoprefab") << "VARIANT\t1\nBASE\t\"project://Cycle.plutoprefab\"\n";
        Require(!scene::Prefab::Instantiate(instances, "project://Cycle.plutoprefab", nullptr, &error) && error.find("cycle") != std::string::npos, "Cycle accepted");
        std::ofstream(assetsPath / "Missing.plutoprefab") << "VARIANT\t1\nBASE\t\"project://Absent.plutoprefab\"\n";
        Require(!scene::Prefab::Preload("project://Missing.plutoprefab").ready, "Missing base accepted");
        std::ofstream(assetsPath / "Main.plutoscene") << "PROPERTY\tPrefab\t2\tproject://Nested.plutoprefab\t0\n";
        assets::ProjectManifest manifest; manifest.startupScene = "project://Main.plutoscene";
        assets::Project project(scratch.root / "Test.plutoproject", manifest);
        std::ofstream(assetsPath / "Named.plutoprefab") << "VARIANT\t1\nBASE\t\"project://Base.plutoprefab\"\nOVERRIDE\t1\t\"Name\"\t\"project://NotAnAsset.png\"\n";
        const auto scan = assets::ScanAssetReferences(assetsPath / "Named.plutoprefab");
        Require(scan.occurrences.size() == 1 && scan.occurrences[0].reference == "project://Base.plutoprefab", "Variant name became an asset dependency");
        assets::ProjectValidationInput validationInput;
        validationInput.assetRoot = assetsPath;
        validationInput.startupScene = manifest.startupScene;
        const auto validation = assets::ValidateProject(validationInput);
        bool cycleDiagnosed = false, missingDiagnosed = false;
        for (const auto &diagnostic : validation.diagnostics)
        {
            if (diagnostic.code == "prefab.cycle") cycleDiagnosed = true;
            if (diagnostic.code == "asset.missing") missingDiagnosed = true;
            Require(diagnostic.owner != "project://Variant.plutoprefab" && diagnostic.owner != "project://Nested.plutoprefab", "Valid variant failed project validation");
        }
        Require(cycleDiagnosed && missingDiagnosed, "Project validation missed variant dependency errors");
        assets::CookOptions options; options.includeUnreferencedAssets = false;
        Require(assets::CookProjectContent(project, scratch.root / "Cooked/Assets", options, &error), error);
        for (auto name : {"Base.plutoprefab", "Variant.plutoprefab", "Nested.plutoprefab", "Stone.plutosurface"})
            Require(std::filesystem::exists(scratch.root / "Cooked/Assets" / name), "Cook omitted base chain");
        core::Engine::GetInstance().GetAssetManager().SetProjectContext((scratch.root / "Cooked").string());
        Require(scene::Prefab::Instantiate(instances, "project://Nested.plutoprefab", nullptr, &error) != nullptr, error);
        auto *added = instances.AddEntity(std::make_unique<scene::Entity>(), nested);
        Require(!scene::Prefab::SaveVariant(*nested, scratch.root / "Rejected.plutoprefab", &error) && !std::filesystem::exists(scratch.root / "Rejected.plutoprefab"), "Structural changes were silently discarded");
        instances.RemoveEntity(added);
        core::Engine::GetInstance().GetAssetManager().ClearProjectContext();
        std::cout << "Prefab variant tests passed\n";
    }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
