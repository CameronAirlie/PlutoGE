#include "PlutoGE/assets/AssetDatabase.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/AssetMetadata.h"
#include "PlutoGE/assets/AssetReferences.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/platform/ContentPack.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/components/ScriptComponent.h"
#include "PlutoGE/scripting/ScriptEngine.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace PlutoGE;
namespace
{
    void Require(bool value, const std::string &message) { if (!value) throw std::runtime_error(message); }
    struct Scratch
    {
        std::filesystem::path parent = std::filesystem::absolute(std::filesystem::temp_directory_path()).lexically_normal();
        std::filesystem::path root = parent / ("PlutoGE-managed-assets-" + assets::GenerateAssetId());
        Scratch() { std::filesystem::create_directories(root / "Assets"); }
        ~Scratch()
        {
            content::UnmountAll();
            if (root.parent_path() == parent && root.filename().string().starts_with("PlutoGE-managed-assets-"))
            { std::error_code error; std::filesystem::remove_all(root, error); }
        }
    };
    void Write(const std::filesystem::path &path, const std::string &bytes)
    {
        std::ofstream stream(path, std::ios::binary); stream << bytes; stream.close();
        Require(static_cast<bool>(stream), "Cannot write managed asset fixture");
    }
    struct ResetEngine
    {
        ~ResetEngine()
        {
            auto &engine = core::Engine::GetInstance();
            engine.SetScene(nullptr);
            engine.GetScriptEngine().Shutdown();
            engine.GetAssetManager().SetAssetSnapshot({}, {});
            engine.GetAssetManager().SetProjectContext({}, "Assets", 1);
        }
    };
}
int main()
try
{
    Scratch scratch;
    ResetEngine reset;
    auto &engine = core::Engine::GetInstance();
    auto &manager = engine.GetAssetManager();
    auto &scripts = engine.GetScriptEngine();
    const auto assetRoot = scratch.root / "Assets";
    assets::ProjectManifest manifest; manifest.assetPipelineVersion = 5; manifest.startupScene = "project://Main.plutoscene";
    assets::Project project(scratch.root / "Test.plutoproject", manifest);
    const std::string entity = "ENTITY\t1\t0\t1\tPrefabTarget\t0,0,0\t0,0,0\t1,1,1\n";
    Write(assetRoot / "Target.plutoprefab", "SCENE\t1\n" + entity);
    Write(assetRoot / "Data.plutoscriptable", "SCRIPTABLE\tPlutoGE.AssetReferenceProbe.ProbeData\nFIELD\tValue\t2\t37\n");
    Write(assetRoot / "Paint.plutomaterial", "Color=0.2,0.4,0.6,1\n");
    Write(assetRoot / "Controls.plutoinput", R"({"Actions":[{"Name":"Use","Bindings":[]}]})");
    Write(assetRoot / "Unused.plutomaterial", "Color=1,0,0,1\n");
    assets::AssetDatabase database;
    std::string error;
    Require(database.Scan(project, &error), error);
    manager.SetProjectContext(scratch.root.string(), "Assets", 5);
    manager.SetAssetSnapshot(database.GetCatalog(), database.GetStorageMap());
    manager.SetLogicalReferenceTypes({assets::ProjectAssetType::Prefab, assets::ProjectAssetType::ScriptableObject,
        assets::ProjectAssetType::Material, assets::ProjectAssetType::InputMapping});
    scripting::ScriptClassDefinition definition;
    definition.namespaceName = "PlutoGE.AssetReferenceProbe"; definition.className = "AssetFieldProbe";
    definition.fields = {
        {.name="Template", .type=scripting::ScriptFieldType::PrefabAsset},
        {.name="Data", .type=scripting::ScriptFieldType::ScriptableObjectAsset},
        {.name="Paint", .type=scripting::ScriptFieldType::MaterialAsset},
        {.name="Controls", .type=scripting::ScriptFieldType::InputMappingAsset},
        {.name="Text", .type=scripting::ScriptFieldType::String},
        {.name="Accepted", .type=scripting::ScriptFieldType::Boolean},
        {.name="SpawnedId", .type=scripting::ScriptFieldType::Int32}
    };
    scripts.RegisterManagedClass(definition);
    scene::Scene authored;
    auto *owner = authored.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name="ScriptOwner"}));
    auto *component = owner->AddComponent(new scene::ScriptComponent(scene::ScriptComponentConfig{.source=definition.GetFullName()}));
    for (const auto &[field, reference] : {std::pair{"Template", "project://Target.plutoprefab"},
        {"Data", "project://Data.plutoscriptable"}, {"Paint", "project://Paint.plutomaterial"},
        {"Controls", "project://Controls.plutoinput"}, {"Text", "project://Unused.plutomaterial"}})
        Require(component->SetFieldValue(field, std::string(reference)), "Cannot assign managed fixture field");
    std::string saved;
    Require(scene::SceneSerializer::SaveToString(authored, saved, &error) && saved.starts_with("SCENE\t3\n"), error);
    Require(std::get<std::string>(*component->GetFieldValue("Template")) == "project://Target.plutoprefab",
        "Scene writer mutated live managed values");
    scripts.Shutdown(); // Simulate reopening while the assembly is unavailable.
    auto missingClass = scene::SceneSerializer::LoadFromString(saved, &error);
    Require(missingClass != nullptr, error);
    auto *restored = missingClass->FindEntityByName("ScriptOwner")->GetComponent<scene::ScriptComponent>();
    Require(restored && restored->GetSerializedFields().empty() && restored->GetFieldValues().size() == 7,
        "Missing-class metadata leaked into managed fields");
    std::string missingSaved;
    Require(scene::SceneSerializer::SaveToString(*missingClass, missingSaved, &error), error);
    Require(missingSaved.find("__Pluto.AssetField.Template") != std::string::npos &&
        std::get<std::string>(*restored->GetFieldValue("Template")).starts_with("asset://"),
        "Missing class discarded asset field roles or logical values");
    manager.SetProjectContext(scratch.root.string(), "Assets", 4);
    std::string rejected = "unchanged";
    Require(!scene::SceneSerializer::SaveToString(*missingClass, rejected, &error) && rejected == "unchanged", "Legacy writer discarded typed field roles or changed caller output");
    manager.SetProjectContext(scratch.root.string(), "Assets", 5);
    auto future = saved;
    const auto schema = future.find("__Pluto.AssetFields.Version\t2\t1");
    Require(schema != std::string::npos, "Missing field schema");
    future.replace(schema, std::string("__Pluto.AssetFields.Version\t2\t1").size(), "__Pluto.AssetFields.Version\t2\t2");
    Require(!scene::SceneSerializer::LoadFromString(future, &error), "Future field schema was silently loaded");
    Write(assetRoot / "Main.plutoscene", missingSaved);
    for (const auto &[before, after] : {std::pair{"Target.plutoprefab", "Moved.plutoprefab"},
        {"Data.plutoscriptable", "Moved.plutoscriptable"}, {"Paint.plutomaterial", "Moved.plutomaterial"},
        {"Controls.plutoinput", "Moved.plutoinput"}})
    {
        std::filesystem::rename(assetRoot / before, assetRoot / after);
        std::filesystem::rename(assets::GetAssetMetadataPath(assetRoot / before), assets::GetAssetMetadataPath(assetRoot / after));
    }
    Require(database.Scan(project, &error), error);
    manager.SetAssetSnapshot(database.GetCatalog(), database.GetStorageMap());
    const auto scan = assets::ScanAssetReferences(assetRoot / "Main.plutoscene", {}, assetRoot);
    Require(scan.errors.empty() && scan.occurrences.size() == 4, "Missing-class scene dependency scan failed");
    assets::CookOptions options; options.includeUnreferencedAssets = false;
    const auto cooked = scratch.root / "Cooked";
    auto wrongKind = missingSaved;
    const auto role = wrongKind.find("__Pluto.AssetField.Paint\t2\tMaterial");
    Require(role != std::string::npos, "Missing material role");
    wrongKind.replace(role, std::string("__Pluto.AssetField.Paint\t2\tMaterial").size(),
        "__Pluto.AssetField.Paint\t2\tPrefab");
    Write(assetRoot / "Main.plutoscene", wrongKind);
    Require(!assets::CookProjectContent(project, cooked / "Assets", options, &error) &&
        !std::filesystem::exists(cooked), "Wrong managed field type reached cooked output");
    Write(assetRoot / "Main.plutoscene", missingSaved);
    Require(assets::CookProjectContent(project, cooked / "Assets", options, &error), error);
    Require(!std::filesystem::exists(cooked / "Assets/Unused.plutomaterial"), "Ordinary managed string cooked an unused sentinel");
    for (const auto *file : {"Moved.plutoprefab", "Moved.plutoscriptable", "Moved.plutomaterial", "Moved.plutoinput"})
        Require(std::filesystem::is_regular_file(cooked / "Assets" / file), "Typed managed dependency was pruned");
    const auto pack = scratch.root / "Assets.plutopack", runtime = scratch.root / "Runtime";
    Require(content::WritePack(cooked, pack, {}, &error) && content::Mount(pack, runtime, &error), error);
    std::filesystem::remove_all(assetRoot);
    std::filesystem::remove_all(cooked);
    std::filesystem::remove_all(scratch.root / "Library");
    manager.SetProjectContext(runtime.string(), "Assets", 5);
    Require(manager.LoadAssetCatalog((runtime / "PlutoAssetCatalog.manifest").string(), &error), error);
    auto loaded = scene::SceneSerializer::LoadFromString(missingSaved, &error);
    Require(loaded != nullptr, error);
    auto *runtimeOwner = loaded->FindEntityByName("ScriptOwner");
    auto *runtimeScript = runtimeOwner->GetComponent<scene::ScriptComponent>();
    Require(runtimeScript != nullptr, "Packed scene lost managed component");
    struct SceneBinding { ~SceneBinding() { core::Engine::GetInstance().SetScene(nullptr); } } binding;
    engine.SetScene(loaded.get());
    scripts.Initialize();
    Require(scripts.LoadAssembly(PLUTOGE_ASSET_REFERENCE_PROBE_ASSEMBLY), scripts.GetLastError());
    const auto reflected = scripts.GetSerializedFields(definition.GetFullName());
    for (const auto &field : definition.fields)
        Require(std::any_of(reflected.begin(), reflected.end(), [&](const auto &actual) {
            return actual.name == field.name && actual.type == field.type;
        }), "Actual managed reflection disagrees with serialized field role: " + field.name);
    auto instance = scripts.CreateInstance(definition.GetFullName());
    Require(instance != nullptr, scripts.GetLastError());
    instance->SetOwner(runtimeOwner);
    instance->ApplyFieldValues(runtimeScript->GetFieldValues());
    instance->OnCreate();
    const auto values = instance->GetFieldValuesSnapshot();
    Require(values.contains("Accepted") && std::get<bool>(values.at("Accepted")) &&
        values.contains("SpawnedId") && std::get<int32_t>(values.at("SpawnedId")) != 0,
        "Real managed callbacks could not load renamed packed prefab/data/input assets");
    Require(manager.LoadMaterialAsset(std::get<std::string>(values.at("Paint"))) != nullptr,
        "Renamed packed managed material field could not resolve");
    Require(std::get<std::string>(values.at("Template")) == std::get<std::string>(*runtimeScript->GetFieldValue("Template")) &&
        std::get<std::string>(values.at("Data")) == std::get<std::string>(*runtimeScript->GetFieldValue("Data")),
        "Managed field round trip changed persistent identities");
    Require(!std::filesystem::exists(runtime), "Managed asset reads extracted packed assets");
    instance.reset();
    engine.SetScene(nullptr);
    scripts.Shutdown();
    std::cout << "Managed asset reference, missing-class and packed runtime checks passed.\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
