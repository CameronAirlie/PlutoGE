#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/ParticleSystemComponent.h"
#include "PlutoGE/platform/ContentPack.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/AssetDatabase.h"
#include "PlutoGE/assets/ProjectValidation.h"
#include "PlutoGE/assets/AssetReferences.h"
#include "PlutoGE/ui/SceneSnapshots.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <source_location>

using namespace PlutoGE;
void Require(bool value, const std::string &message,
             const std::source_location location = std::source_location::current())
{
    if (!value) throw std::runtime_error("Line " + std::to_string(location.line()) + ": " + message);
}
struct Scratch
{
    std::filesystem::path root = std::filesystem::temp_directory_path() / ("PlutoGE-variants-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~Scratch() { content::UnmountAll(); if (root.parent_path() == std::filesystem::temp_directory_path() && root.filename().string().starts_with("PlutoGE-variants-")) { std::error_code ec; std::filesystem::remove_all(root, ec); } }
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
        // Scene reload refreshes prefab instances after deserialization.
        // Keep local trees under both the root and inherited descendants.
        {
            scene::Scene localScene;
            auto *root = scene::Prefab::Instantiate(localScene, "project://Base.plutoprefab", nullptr, &error);
            Require(root != nullptr, error);
            const auto rootId = root->GetID();
            auto *local = localScene.AddEntity(std::make_unique<scene::Entity>(), root->GetChildren()[0]);
            const auto localId = local->GetID();
            local->SetName("Scene child");
            local->SetPosition({7,8,9});
            local->CreateComponent<scene::ColliderComponent>()->SetRadius(3);
            auto *grandchild = localScene.AddEntity(std::make_unique<scene::Entity>(), local);
            const auto grandchildId = grandchild->GetID();
            auto *nestedPrefab = scene::Prefab::Instantiate(localScene, "project://Base.plutoprefab", root, &error);
            Require(nestedPrefab != nullptr, error);
            const auto nestedId = nestedPrefab->GetID();
            nestedPrefab->SetName("Local nested prefab"); nestedPrefab->AddPrefabOverride("Name");
            Require(scene::SceneSerializer::SaveToString(localScene, snapshot, &error), error);
            for (int reload = 0; reload < 2; ++reload)
            {
                auto loaded = scene::SceneSerializer::LoadFromString(snapshot, &error);
                Require(loaded != nullptr, error);
                Require(scene::Prefab::UpdateInstances(*loaded, {}, &error) == 1, error);
                auto *saved = loaded->FindEntityByID(localId);
                Require(saved && saved->GetName() == "Scene child" && saved->GetPosition().x == 7 &&
                        saved->GetComponent<scene::ColliderComponent>()->GetRadius() == 3,
                        "Scene-added child lost its identity or properties on refresh");
                Require(saved->GetParent()->GetPrefabEntityID() == child->GetID() &&
                        loaded->FindEntityByID(grandchildId)->GetParent() == saved,
                        "Scene-added hierarchy was lost on refresh");
                Require(loaded->FindEntityByID(nestedId)->GetParent() == loaded->FindEntityByID(rootId) &&
                        loaded->FindEntityByID(nestedId)->GetName() == "Local nested prefab",
                        "Nested prefab addition was lost on refresh");
                Require(scene::SceneSerializer::SaveToString(*loaded, snapshot, &error), error);
                Require(scene::Prefab::RevertInstance(*loaded->FindEntityByID(rootId), &error), error);
                Require(!loaded->FindEntityByID(localId) && !loaded->FindEntityByID(nestedId),
                        "Revert did not discard added children");
            }
        }
        {
            scene::Scene sourceScene;
            auto *sourceRoot = sourceScene.AddEntity(std::make_unique<scene::Entity>());
            auto *sourceA = sourceScene.AddEntity(std::make_unique<scene::Entity>(), sourceRoot);
            auto *sourceB = sourceScene.AddEntity(std::make_unique<scene::Entity>(), sourceA);
            Require(scene::Prefab::SaveFromEntity(*sourceRoot, assetsPath / "Reparent.plutoprefab", &error), error);
            for (bool localParent : {false, true})
            {
                scene::Scene edited;
                auto *root = scene::Prefab::Instantiate(edited, "project://Reparent.plutoprefab", nullptr, &error);
                Require(root != nullptr, error);
                const auto rootId = root->GetID();
                auto *a = root->GetChildren()[0];
                auto *b = a->GetChildren()[0];
                b->SetParent(root);
                auto *parent = localParent ? edited.AddEntity(std::make_unique<scene::Entity>(), b) : b;
                const auto localId = parent->GetID();
                a->SetParent(parent); // Reverse inherited ancestry, optionally through a local wrapper.
                a->SetName("Reparented override"); a->AddPrefabOverride("Name");
                Require(scene::SceneSerializer::SaveToString(edited, snapshot, &error), error);
                for (int reload = 0; reload < 3; ++reload)
                {
                    auto loaded = scene::SceneSerializer::LoadFromString(snapshot, &error);
                    Require(loaded && scene::Prefab::UpdateInstances(*loaded, {}, &error) == 1, error);
                    auto *savedRoot = loaded->FindEntityByID(rootId);
                    scene::Entity *savedA = nullptr, *savedB = nullptr;
                    int count = 0, countA = 0, countB = 0;
                    const auto inspect = [&](auto &&self, scene::Entity *entity) -> void
                    {
                        ++count;
                        if (entity->GetPrefabEntityID() == sourceA->GetID()) { savedA = entity; ++countA; }
                        if (entity->GetPrefabEntityID() == sourceB->GetID()) { savedB = entity; ++countB; }
                        for (auto *childEntity : entity->GetChildren()) self(self, childEntity);
                    };
                    inspect(inspect, savedRoot);
                    Require(countA == 1 && countB == 1 && count == (localParent ? 4 : 3),
                            "Reparenting duplicated inherited entities on reload");
                    auto *savedParent = localParent ? loaded->FindEntityByID(localId) : savedB;
                    Require(savedA->GetParent() == savedParent && savedB->GetParent() == savedRoot &&
                            (!localParent || savedParent->GetParent() == savedB),
                            "Reparented hierarchy reverted on reload");
                    Require(savedA->GetName() == "Reparented override", "Reparenting lost property overrides");
                    Require(scene::SceneSerializer::SaveToString(*loaded, snapshot, &error), error);
                    Require(scene::Prefab::RevertInstance(*savedRoot, &error), error);
                    Require(savedRoot->GetChildren().size() == 1 &&
                            savedRoot->GetChildren()[0]->GetPrefabEntityID() == sourceA->GetID() &&
                            savedRoot->GetChildren()[0]->GetChildren().size() == 1 &&
                            savedRoot->GetChildren()[0]->GetChildren()[0]->GetPrefabEntityID() == sourceB->GetID(),
                            "Revert did not restore the source hierarchy");
                }
            }
        }
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

        // Exported assets exist only in a mounted pack, never as loose files.
        const auto cooked = scratch.root / "Cooked";
        std::ofstream(cooked / "Assets/Impact.plutoparticles")
            << "ParticleSystemVersion=2\nPlayOnAwake=false\nMaxParticles=64\nEmissionRateOverTime=0\n";
        std::ofstream(cooked / "Assets/Impact.plutoprefab")
            << "SCENE\t1\nENTITY\t1\t0\t1\tImpact\t0,0,0\t0,0,0\t1,1,1\n"
               "COMPONENT\t1\tParticleSystemComponent\t1\n"
               "PROPERTY\tParticleSystemAsset\t2\tproject://Impact.plutoparticles\t0\nEND_COMPONENT\n";
        const auto pack = scratch.root / "Game.plutopack";
        const auto mounted = scratch.root / "Runtime";
        Require(content::WritePack(cooked, pack, {}, &error), error);
        Require(content::Mount(pack, mounted, &error), error);
        core::Engine::GetInstance().GetAssetManager().SetProjectContext(mounted.string());
        Require(!std::filesystem::exists(mounted / "Assets/Base.plutoprefab"), "Packed fixture has loose files");
        const auto preload = scene::Prefab::Preload("project://Base.plutoprefab");
        Require(preload.ready, "Packed prefab preload failed: " + preload.error);
        Require(scene::Prefab::IsReady("project://Base.plutoprefab"), "Packed prefab cache is not ready");
        Require(scene::Prefab::Preload("project://Base.plutoprefab").cacheHit, "Packed prefab cache missed");
        auto *packedBase = scene::Prefab::Instantiate(instances, "project://Base.plutoprefab", nullptr, &error);
        Require(packedBase && packedBase->GetComponent<scene::ColliderComponent>(), "Packed pickup prefab failed: " + error);
        auto *packedNested = scene::Prefab::Instantiate(instances, "project://Nested.plutoprefab", nullptr, &error);
        Require(packedNested && packedNested->GetName() == "Applied" && packedNested->GetScale().x == 3,
                "Packed variant inheritance failed: " + error);
        auto *impact = scene::Prefab::Instantiate(instances, "project://Impact.plutoprefab", nullptr, &error);
        Require(impact != nullptr, "Packed impact prefab failed: " + error);
        auto *particles = impact->GetComponent<scene::ParticleSystemComponent>();
        Require(particles && particles->GetMaxParticles() == 64 && particles->GetEmissionRateOverTime() == 0,
                "Packed particle asset was not loaded");
        particles->Emit(12);
        Require(particles->ConsumePendingEmitCount() == 12, "Packed particle burst was not queued");
        Require(assets::AssetDatabase::HashFile(mounted / "Assets/Base.plutoprefab") ==
                assets::AssetDatabase::HashFile(cooked / "Assets/Base.plutoprefab"), "Packed dependency hash differs from loose file");
        content::UnmountAll();
        Require(!scene::Prefab::IsReady("project://Base.plutoprefab"), "Unmounted prefab remained ready");
        Require(!scene::Prefab::Preload("project://Base.plutoprefab").ready, "Unmounted prefab remained loadable");
        core::Engine::GetInstance().GetAssetManager().ClearProjectContext();
        std::cout << "Prefab variant tests passed\n";
    }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
