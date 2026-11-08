// ScriptBehaviour lifecycle through the hosted .NET runtime: error reporting, faulted instances.
// Probes live in tests/managed/PlutoGE.ScriptBehaviourProbe.

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scripting/ScriptEngine.h"
#include "PlutoGE/scripting/ScriptErrors.h"

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

    std::cout << "ScriptBehaviourTests passed\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << "ScriptBehaviourTests failed: " << error.what() << '\n';
    return 1;
}
