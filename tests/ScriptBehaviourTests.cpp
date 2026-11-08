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
#include <cmath>
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

    const scripting::ScriptFieldDefinition &Field(const scripting::ScriptClassDefinition &type, const char *name)
    {
        const auto *field = scripting::FindFieldDefinition(type, name);
        Require(field != nullptr, std::string("Field not reflected: ") + name);
        return *field;
    }

    void FieldTextCodec()
    {
        using scripting::ScriptFieldType;
        Require(scripting::FormatFieldValueText(17.7f) == "17.7", "Floats use the shortest round-trip text");
        Require(scripting::FormatFieldValueText(glm::vec3(1.0f, 0.5f, -2.0f)) == "1,0.5,-2", "Vector text");
        Require(std::get<float>(scripting::ParseFieldValueText(ScriptFieldType::Float, "1.500000")) == 1.5f, "Legacy float text");
        Require(std::get<int32_t>(scripting::ParseFieldValueText(ScriptFieldType::Int32, "abc")) == 0, "Malformed int");
        Require(std::get<glm::vec4>(scripting::ParseFieldValueText(ScriptFieldType::Color, "")) == glm::vec4(1.0f), "Colour default is white");
        Require(std::get<glm::vec4>(scripting::ParseFieldValueText(ScriptFieldType::Quaternion, "0,0,0")) == glm::vec4(0, 0, 0, 1),
                "Missing quaternion component keeps identity");
        const glm::vec4 color(0.25f, 0.5f, 0.75f, 1.0f);
        Require(std::get<glm::vec4>(scripting::ParseFieldValueText(ScriptFieldType::Color, scripting::FormatFieldValueText(color))) == color,
                "Colour round trip");
        Require(std::get<std::string>(scripting::ParseFieldValueText(ScriptFieldType::Enum, "Attack")) == "Attack", "Enum by name");

        std::vector<scripting::ScriptFieldDefinition> fields(1);
        fields[0].name = "Count";
        fields[0].formerNames = {"oldCount"};
        std::unordered_map<std::string, scripting::ScriptFieldValue> values{{"oldCount", int32_t{5}}};
        Require(scripting::MigrateRenamedFields(fields, values) && values.size() == 1 && std::get<int32_t>(values.at("Count")) == 5,
                "Former field names migrate");
        values = {{"oldCount", int32_t{5}}, {"Count", int32_t{6}}};
        scripting::MigrateRenamedFields(fields, values);
        Require(values.size() == 1 && std::get<int32_t>(values.at("Count")) == 6, "The current name wins over a former one");
    }

    void FieldTypesAndAttributes(scripting::ScriptEngine &scripts, scene::Scene &scene)
    {
        const auto *type = scripts.FindClass("PlutoGE.ScriptBehaviourProbe.FieldTypesProbe");
        Require(type != nullptr, "FieldTypesProbe not reflected");

        const auto &mode = Field(*type, "Mode");
        Require(mode.type == scripting::ScriptFieldType::Enum && (mode.enumNames == std::vector<std::string>{"Idle", "Patrol", "Attack"}),
                "Enum field and members");
        Require(std::get<std::string>(mode.defaultValue) == "Patrol", "Enum default by name");
        Require(mode.header == "Behaviour" && mode.tooltip == "What the probe does", "Header and tooltip");
        const auto &speed = Field(*type, "Speed");
        Require(speed.range && speed.range->x == 0.0 && speed.range->y == 10.0, "Range");
        Require(Field(*type, "Tint").type == scripting::ScriptFieldType::Color &&
                    std::get<glm::vec4>(Field(*type, "Tint").defaultValue) == glm::vec4(1.0f, 0.5f, 0.25f, 1.0f),
                "Colour field");
        Require(Field(*type, "Weights").type == scripting::ScriptFieldType::Vector4, "Vector4 field");
        Require(std::get<glm::vec4>(Field(*type, "Facing").defaultValue) == glm::vec4(0, 0, 0, 1), "Quaternion field");
        const auto &levels = Field(*type, "Levels");
        Require(levels.isArray && levels.type == scripting::ScriptFieldType::Int32 &&
                    (std::get<scripting::ScriptReferenceArray>(levels.defaultValue) == scripting::ScriptReferenceArray{"1", "2", "3"}),
                "Int array");
        Require(Field(*type, "Names").isArray && Field(*type, "Names").type == scripting::ScriptFieldType::String, "String list");
        Require((std::get<scripting::ScriptReferenceArray>(Field(*type, "Sequence").defaultValue) == scripting::ScriptReferenceArray{"Idle", "Attack"}),
                "Enum array");
        Require((Field(*type, "Count").formerNames == std::vector<std::string>{"oldCount"}), "Former names");
        Require(scripting::FindFieldDefinition(*type, "Unsupported") == nullptr, "Unsupported members are skipped");

        auto *owner = scene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Fields"}));
        auto instance = Create(scripts, "FieldTypesProbe", owner);
        instance->ApplyFieldValues({
            {"Mode", std::string("Attack")},
            {"Tint", glm::vec4(0.0f, 1.0f, 0.0f, 1.0f)},
            {"Facing", glm::vec4(0.0f, 0.70710677f, 0.0f, 0.70710677f)},
            {"Levels", scripting::ScriptReferenceArray{"7", "8"}},
            {"Sequence", scripting::ScriptReferenceArray{"Patrol"}},
            {"oldCount", int32_t{9}},
        });
        const auto values = instance->GetFieldValuesSnapshot();
        Require(std::get<std::string>(values.at("Mode")) == "Attack", "Enum value applied");
        Require(std::get<glm::vec4>(values.at("Tint")) == glm::vec4(0.0f, 1.0f, 0.0f, 1.0f), "Colour value applied");
        Require(std::abs(std::get<glm::vec4>(values.at("Facing")).y - 0.70710677f) < 1e-5f, "Quaternion value applied");
        Require((std::get<scripting::ScriptReferenceArray>(values.at("Levels")) == scripting::ScriptReferenceArray{"7", "8"}), "Int array applied");
        Require((std::get<scripting::ScriptReferenceArray>(values.at("Sequence")) == scripting::ScriptReferenceArray{"Patrol"}), "Enum array applied");
        Require(std::get<int32_t>(values.at("Count")) == 9, "Value stored under the former name applied");
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
    FieldTextCodec();
    FieldTypesAndAttributes(scripts, scene);
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
