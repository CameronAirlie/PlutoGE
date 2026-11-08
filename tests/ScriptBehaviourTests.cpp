// ScriptBehaviour lifecycle through the hosted .NET runtime: error reporting, faulted instances.
// Probes live in tests/managed/PlutoGE.ScriptBehaviourProbe.

#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/ScriptComponent.h"
#include "PlutoGE/scripting/ScriptEngine.h"
#include "PlutoGE/scripting/ScriptErrors.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace PlutoGE;

namespace
{
    void Require(bool value, const std::string &message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    int32_t IntField(const scripting::ScriptInstance &instance, const char *name)
    {
        const auto value = instance.GetFieldValue(name);
        Require(value && std::holds_alternative<int32_t>(*value), std::string("Probe field missing: ") + name);
        return std::get<int32_t>(*value);
    }

    std::unique_ptr<scripting::ScriptInstance> Create(scripting::ScriptEngine &scripts, const char *className, scene::Entity *owner)
    {
        auto instance = scripts.CreateInstance(std::string("PlutoGE.ScriptBehaviourProbe.") + className);
        Require(instance != nullptr, scripts.GetLastError());
        instance->SetOwner(owner);
        return instance;
    }

    void ExceptionsAreReported(scripting::ScriptEngine &scripts, scene::Scene &scene)
    {
        std::vector<scripting::ScriptErrorReport> reports;
        scripting::SetScriptErrorListener([&](const scripting::ScriptErrorReport &report) { reports.push_back(report); });
        struct ClearListener { ~ClearListener() { scripting::ClearScriptErrorListener(); } } clear;

        auto *owner = scene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Faulty"}));
        auto createFault = Create(scripts, "CreateFaultProbe", owner);
        createFault->OnCreate();
        Require(createFault->IsFaulted(), "A throwing OnCreate did not fault the instance");
        Require(reports.size() == 1, "OnCreate exception was not reported");
        const auto &created = reports.front();
        Require(created.callback == "OnCreate" && created.entityId == owner->GetID() && created.entityName == "Faulty",
                "Report lacks callback or entity context: " + scripting::FormatScriptErrorReport(created));
        Require(created.exception.find("probe create failure") != std::string::npos &&
                    created.exception.find("CreateFaultProbe.OnCreate") != std::string::npos,
                "Report lacks the exception message or stack trace: " + created.exception);

        reports.clear();
        auto updateFault = Create(scripts, "UpdateFaultProbe", owner);
        updateFault->OnCreate();
        Require(!updateFault->IsFaulted() && reports.empty(), "A clean OnCreate was reported as failing");
        for (int frame = 0; frame < 25; ++frame)
            updateFault->OnUpdate(1.0f / 60.0f);
        Require(IntField(*updateFault, "Updates") == 25, "A throwing OnUpdate stopped later updates");
        Require(reports.size() == 2 && reports[0].occurrences == 1 && reports[1].occurrences == 10,
                "Repeated OnUpdate exceptions were not throttled to 1, 10: got " + std::to_string(reports.size()) + " reports");
        Require(reports[0].callback == "OnUpdate" && reports[0].exception.find("UpdateFaultProbe.Fail") != std::string::npos,
                "OnUpdate report lacks its stack trace");
    }

    scene::ScriptComponent *AddScript(scene::Scene &scene, const char *name, const char *className,
                                      std::unordered_map<std::string, scripting::ScriptFieldValue> fields = {})
    {
        auto *entity = scene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = name}));
        return entity->CreateComponent<scene::ScriptComponent>(scene::ScriptComponentConfig{
            .scriptClass = std::string("PlutoGE.ScriptBehaviourProbe.") + className, .fieldValues = std::move(fields)});
    }

    template <typename T>
    T LiveField(const scene::ScriptComponent &script, const char *name)
    {
        const auto value = script.GetFieldValue(name);
        Require(value && std::holds_alternative<T>(*value), std::string("Live field missing: ") + name);
        return std::get<T>(*value);
    }

    // Two-phase start (every OnCreate can see every script) and configured spawning (position, parent and
    // configure callback in place before the spawned script's OnCreate).
    void StartOrderAndSpawning(core::Engine &engine)
    {
        const auto root = std::filesystem::temp_directory_path() /
                          ("PlutoGE-behaviour-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        struct Cleanup
        {
            std::filesystem::path root;
            ~Cleanup() { std::error_code ec; std::filesystem::remove_all(root, ec); }
        } cleanup{root};
        engine.GetAssetManager().SetProjectContext(root.string());

        std::string error;
        {
            scene::Scene authoring;
            auto *spawned = AddScript(authoring, "Spawned", "SpawnedProbe")->GetOwner();
            Require(scene::Prefab::SaveFromEntity(*spawned, root / "Assets" / "Spawned.plutoprefab", &error), error);
        }

        scene::Scene scene;
        auto *sibling = AddScript(scene, "Sibling", "SiblingProbe");
        AddScript(scene, "Partner", "PartnerProbe", {{"Configured", int32_t{5}}});
        auto *spawner = AddScript(scene, "Spawner", "SpawnerProbe",
                                  {{"PrefabReference", std::string("project://Spawned.plutoprefab")}});
        engine.SetScene(&scene);
        engine.StartRuntime();
        struct Stop { core::Engine &engine; ~Stop() { engine.StopRuntime(); engine.SetScene(nullptr); } } stop{engine};

        Require(LiveField<bool>(*sibling, "SawPartner"), "OnCreate ran before a later script was created and configured");
        Require(LiveField<bool>(*spawner, "SpawnWorked"), "Spawn configure failed: " + LiveField<std::string>(*spawner, "Failure"));
        const auto *spawnerEntity = spawner->GetOwner();
        Require(spawnerEntity->GetChildren().size() == 1 && spawnerEntity->GetChildren().front()->GetName() == "Spawned",
                "Spawned prefab was not parented");
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

    scene::Scene scene;
    engine.SetScene(&scene);
    scripts.Initialize();
    Require(scripts.LoadAssembly(PLUTOGE_SCRIPT_BEHAVIOUR_PROBE_ASSEMBLY), scripts.GetLastError());

    ExceptionsAreReported(scripts, scene);
    engine.SetScene(nullptr);
    StartOrderAndSpawning(engine);

    std::cout << "ScriptBehaviourTests passed\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << "ScriptBehaviourTests failed: " << error.what() << '\n';
    return 1;
}
