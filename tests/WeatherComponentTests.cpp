// WeatherComponent: the scene's weather state (wetness, precipitation) and how the renderer finds it; particle
// assets' Scene Wind Drag round-trip.
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/render/SceneEnvironment.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/WeatherComponent.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace PlutoGE;

namespace
{
    void Check(bool value, const char *message)
    {
        if (!value)
            throw std::runtime_error(message);
    }
}

int main()
try
{
    // State: clamped, and kept through serialization; bad text keeps the current value.
    {
        scene::WeatherComponent weather;
        weather.SetWetness(1.5f);
        weather.SetPrecipitation(-1.0f);
        Check(weather.GetWetness() == 1.0f && weather.GetPrecipitation() == 0.0f, "Weather values are clamped to 0..1");
        weather.Deserialize({{"Wetness", scene::PropertyType::Float, "0.6"}, {"Precipitation", scene::PropertyType::Float, "0.8"}});
        scene::WeatherComponent restored;
        restored.Deserialize(weather.Serialize());
        Check(restored.GetWetness() > 0.59f && restored.GetWetness() < 0.61f && restored.GetPrecipitation() > 0.79f,
              "Weather survives serialization");
        restored.Deserialize({{"Wetness", scene::PropertyType::Float, "rain"}});
        Check(restored.GetWetness() > 0.59f, "Malformed text keeps the current value");
    }

    // The scene: dry without a weather component; the first enabled one on an active entity drives it.
    {
        scene::Scene scene;
        Check(render::BuildSceneWeather(&scene).Dry() && render::BuildSceneWeather(nullptr).Dry(), "A scene without weather is dry");

        auto *disabledEntity = scene.AddEntity(std::make_unique<scene::Entity>());
        auto *disabled = disabledEntity->CreateComponent<scene::WeatherComponent>();
        disabled->SetWetness(0.2f);
        disabled->SetEnabled(false);
        auto *inactiveEntity = scene.AddEntity(std::make_unique<scene::Entity>());
        inactiveEntity->CreateComponent<scene::WeatherComponent>()->SetWetness(0.3f);
        inactiveEntity->SetActive(false);
        auto *activeEntity = scene.AddEntity(std::make_unique<scene::Entity>());
        auto *active = activeEntity->CreateComponent<scene::WeatherComponent>();
        active->SetWetness(0.7f);
        active->SetPrecipitation(0.9f);
        const auto weather = render::BuildSceneWeather(&scene);
        Check(weather.wetness == 0.7f && weather.precipitation == 0.9f,
              "The first enabled weather on an active entity drives the scene");
        const auto lighting = render::BuildSceneLighting(render::CameraData{}, &scene);
        Check(lighting.weather == weather, "Scene lighting carries the weather to the renderer");
    }

    // Particle assets keep their Scene Wind Drag.
    {
        const auto folder = std::filesystem::temp_directory_path() / "PlutoGEWeatherComponentTests";
        std::filesystem::create_directories(folder);
        const auto path = (folder / "Rain.plutoparticles").string();
        assets::AssetManager manager;
        assets::ParticleSystemAsset rain;
        rain.sceneWindDrag = 0.5f;
        std::string error;
        Check(manager.SaveParticleSystemAsset(path, rain, &error), error.c_str());
        bool loaded = false;
        const auto reloaded = manager.LoadParticleSystemAsset(path, &loaded);
        Check(loaded && reloaded.sceneWindDrag == 0.5f, "A particle asset keeps its Scene Wind Drag");
        std::filesystem::remove_all(folder);
    }

    std::cout << "WeatherComponentTests passed\n";
    return 0;
}
catch (const std::exception &exception)
{
    std::cerr << "WeatherComponentTests failed: " << exception.what() << '\n';
    return 1;
}
