#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/VolumetricCloudComponent.h"
#include "PlutoGE/scripting/ScriptEngine.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

using namespace PlutoGE;

// A C# weather script drives VolumetricCloudComponent through the generic property bridge
// (PlutoGE.ScriptCore VolumetricCloudComponent); the native component must follow.
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

    scene::Scene scene;
    auto *deck = scene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Clouds"}));
    auto *clouds = deck->CreateComponent<scene::VolumetricCloudComponent>();
    clouds->SetCoverage(0.4f);
    auto *owner = scene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Weather"}));

    engine.SetScene(&scene);
    scripts.Initialize();
    Require(scripts.LoadAssembly(PLUTOGE_WEATHER_PROBE_ASSEMBLY), scripts.GetLastError());
    auto instance = scripts.CreateInstance("PlutoGE.WeatherProbe.WeatherProbe");
    Require(instance != nullptr, scripts.GetLastError());
    instance->SetOwner(owner);
    instance->OnCreate();

    const auto values = instance->GetFieldValuesSnapshot();
    Require(Field<std::string>(values, "Failure").empty(), "Probe failed: " + Field<std::string>(values, "Failure"));
    Require(Field<bool>(values, "Found"), "The script did not find the cloud component");
    Require(std::abs(Field<float>(values, "InitialCoverage") - 0.4f) < 1e-4f, "The script read the wrong coverage");
    Require(Field<bool>(values, "RoundTrips"), "Cloud properties did not round-trip through the bridge");
    Require(clouds->GetCoverage() == 1.0f, "Scripted coverage did not reach the native component, clamped");
    Require(std::abs(clouds->GetDensity() - 0.8f) < 1e-4f && std::abs(clouds->GetWindSpeed() - 14.0f) < 1e-4f,
            "Scripted density or wind speed did not reach the native component");
    Require(clouds->GetWindDirection() == glm::vec3(0.0f, 0.0f, 1.0f), "Scripted wind direction did not reach native");
    Require(!clouds->GetCastShadows() && std::abs(clouds->GetShadowDistance() - 5000.0f) < 1e-2f &&
                std::abs(clouds->GetShadowDensity() - 0.5f) < 1e-4f,
            "Scripted cloud shadow settings did not reach the native component");
    std::cout << "Cloud scripting: weather probe drove the cloud component\n";
    return 0;
}
catch (const std::exception &exception)
{
    std::cerr << exception.what() << '\n';
    return 1;
}
