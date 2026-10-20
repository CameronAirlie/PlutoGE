#include "PlutoGE/render/PhysicalSkyModel.h"
#include "PlutoGE/render/SceneEnvironment.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/PhysicalSkyComponent.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

using namespace PlutoGE;

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    bool Near(float a, float b, float tolerance = 1.0e-4f) { return std::abs(a - b) <= tolerance; }
    bool Near(const glm::vec3 &a, const glm::vec3 &b, float tolerance = 1.0e-4f)
    {
        return glm::length(a - b) <= tolerance;
    }

    glm::vec3 Elevated(float elevationDegrees, float azimuthDegrees = 200.0f)
    {
        const float e = glm::radians(elevationDegrees), a = glm::radians(azimuthDegrees);
        return {std::cos(e) * std::sin(a), std::sin(e), -std::cos(e) * std::cos(a)};
    }

    float Luminance(const render::CelestialLight &light)
    {
        return glm::dot(light.color, glm::vec3(0.2126f, 0.7152f, 0.0722f)) * light.intensity;
    }

    void TestMoonPhases()
    {
        render::PhysicalSkySettings sky;
        const glm::vec3 sun = Elevated(25.0f);

        sky.moonPhase = 0.5f;
        const auto full = render::ResolveCelestialState(sky, sun);
        Require(Near(full.moonDirection, -full.sunDirection), "A full moon should sit opposite the sun");
        Require(Near(full.moonIlluminatedFraction, 1.0f) && Near(full.moonPhaseBrightness, 1.0f, 1.0e-3f),
                "A full moon should be fully lit at full brightness");

        sky.moonPhase = 0.0f;
        const auto newMoon = render::ResolveCelestialState(sky, sun);
        Require(Near(newMoon.moonDirection, newMoon.sunDirection), "A new moon should sit with the sun");
        Require(newMoon.moonIlluminatedFraction < 1.0e-4f, "A new moon should be unlit");

        // Waxing quarter: half lit, but roughly a tenth of full-moon brightness.
        sky.moonPhase = 0.25f;
        const auto quarter = render::ResolveCelestialState(sky, Elevated(0.0f, 270.0f));
        Require(Near(quarter.moonIlluminatedFraction, 0.5f, 0.05f), "A quarter moon should be half lit");
        Require(quarter.moonPhaseBrightness > 0.06f && quarter.moonPhaseBrightness < 0.13f,
                "Quarter-moon brightness should follow the lunar phase law");
        // The waxing moon trails the sun eastward: at sunset in the west it is high in the south.
        Require(quarter.moonDirection.y > 0.3f, "A waxing quarter moon should be up at sunset");

        Require(render::MoonPhaseBrightness(glm::radians(150.0f)) < 0.01f, "A thin crescent should be faint");
    }

    void TestCelestialFrame()
    {
        render::PhysicalSkySettings sky;
        sky.latitude = 52.0f;
        sky.northAzimuth = 30.0f;
        const auto state = render::ResolveCelestialState(sky, Elevated(40.0f, 140.0f));
        Require(Near(state.celestialPole.y, std::sin(glm::radians(52.0f))), "The pole should sit at the latitude");
        Require(Near(glm::determinant(state.worldToCelestial), 1.0f), "The star frame should be a rotation");
        Require(Near(state.worldToCelestial * state.celestialPole, glm::vec3(0, 0, 1)),
                "The celestial pole should map to the frame's pole");

        // Turning the sun around the pole turns the stars with it.
        const glm::quat turn = glm::angleAxis(glm::radians(37.0f), state.celestialPole);
        const auto turned = render::ResolveCelestialState(sky, turn * state.sunDirection);
        const glm::vec3 star = glm::normalize(glm::vec3(0.3f, 0.7f, -0.2f));
        Require(Near(state.worldToCelestial * star, turned.worldToCelestial * (turn * star), 1.0e-3f),
                "Stars should stay fixed relative to the sun's diurnal motion");

        sky.moonPlacement = render::MoonPlacement::Manual;
        sky.northAzimuth = 0.0f;
        sky.moonAzimuth = 90.0f;
        sky.moonElevation = 0.0f;
        Require(Near(render::ResolveCelestialState(sky, Elevated(10.0f)).moonDirection, glm::vec3(1, 0, 0)),
                "Manual placement should measure azimuth from north (-Z) toward east (+X)");
    }

    void TestCelestialLight()
    {
        render::PhysicalSkySettings sky;
        const glm::vec3 color(1.0f);
        const float illuminance = 4.0f;

        const auto noon = render::ResolveCelestialState(sky, Elevated(60.0f));
        const auto day = render::ResolveCelestialLight(sky, noon, color, illuminance);
        Require(!day.isMoon && Near(day.direction, -noon.sunDirection) && day.intensity > 3.9f,
                "Daytime should be lit by the sun");

        const auto midnight = render::ResolveCelestialState(sky, Elevated(-50.0f));
        const auto night = render::ResolveCelestialLight(sky, midnight, color, illuminance);
        Require(night.isMoon && Near(night.direction, -midnight.moonDirection), "Night should be lit by the moon");
        Require(Near(night.intensity, illuminance * sky.moonLightIntensity, 1.0e-3f),
                "Full-moon light should be the configured fraction of the sun light");

        sky.moonCastsLight = false;
        const auto dark = render::ResolveCelestialLight(sky, midnight, color, illuminance);
        Require(!dark.isMoon && dark.intensity == 0.0f, "Disabling moonlight should leave the night unlit");
        sky.moonCastsLight = true;

        // Sweep through dusk: the light must fade out and back in without a jump.
        float previous = -1.0f;
        bool previousMoon = false;
        for (float elevation = 6.0f; elevation >= -30.0f; elevation -= 0.05f)
        {
            const auto light = render::ResolveCelestialLight(sky, render::ResolveCelestialState(sky, Elevated(elevation)),
                                                             color, illuminance);
            const float luminance = Luminance(light);
            if (previous >= 0.0f && light.isMoon != previousMoon)
                Require(luminance < 1.0e-3f && previous < 1.0e-3f, "The sun/moon handoff should happen in darkness");
            previous = luminance;
            previousMoon = light.isMoon;
        }
        Require(previousMoon, "Deep night should end lit by the moon");
    }

    void TestEnvironment()
    {
        render::PhysicalSkySettings sky;
        const glm::vec3 up(0, 1, 0);
        const auto luminance = [](const glm::vec3 &c) { return glm::dot(c, glm::vec3(0.2126f, 0.7152f, 0.0722f)); };
        const float day = luminance(render::EvaluatePhysicalSkyEnvironment(sky, render::ResolveCelestialState(sky, Elevated(45.0f)), up));
        const auto midnight = Elevated(-50.0f);
        const float fullMoon = luminance(render::EvaluatePhysicalSkyEnvironment(sky, render::ResolveCelestialState(sky, midnight), up));
        sky.moonPhase = 0.0f;
        const float newMoon = luminance(render::EvaluatePhysicalSkyEnvironment(sky, render::ResolveCelestialState(sky, midnight), up));
        Require(fullMoon < day * 0.05f, "The night sky should be far darker than day");
        Require(fullMoon > newMoon * 1.5f, "Moonlight should brighten the night sky");
        Require(newMoon > 0.0f, "Airglow should keep the moonless sky above black");

        sky.lightPollutionIntensity = 0.05f;
        const glm::vec3 horizon = glm::normalize(glm::vec3(0.0f, 0.05f, -1.0f));
        const auto state = render::ResolveCelestialState(sky, midnight);
        sky.lightPollutionIntensity = 0.0f;
        const float clean = luminance(render::EvaluatePhysicalSkyEnvironment(sky, state, horizon));
        sky.lightPollutionIntensity = 0.05f;
        Require(luminance(render::EvaluatePhysicalSkyEnvironment(sky, state, horizon)) > clean,
                "Light pollution should brighten the night horizon");

        const auto lanes = render::PackPhysicalSkyParameters(sky, state);
        Require(Near(glm::vec3(lanes[PLUTO_SKY_LANE_MOON]), state.moonDirection), "The moon lane should carry its direction");
        Require(Near(glm::vec3(lanes[PLUTO_SKY_LANE_SUN]), state.sunDirection), "The sun lane should carry its direction");
    }

    // Scene lighting hands the directional light to the moon at night, while
    // the sky packet keeps the true sun.
    void TestSceneIntegration()
    {
        scene::Scene world;
        world.AddEntity(std::make_unique<scene::Entity>())->CreateComponent<scene::PhysicalSkyComponent>();
        auto *sun = world.AddEntity(std::make_unique<scene::Entity>())->CreateComponent<scene::LightComponent>();
        sun->SetLightType(scene::LightType::Directional);
        sun->SetIntensity(4.0f);
        const render::CameraData camera{};

        const glm::vec3 toSun = Elevated(-50.0f);
        sun->SetDirection(-toSun);
        const auto night = render::BuildSceneLighting(camera, &world);
        Require(night.directionalIsMoon && night.directionalIntensity > 0.0f, "Scene lighting should be moonlit at night");
        Require(Near(night.sunDirection, toSun), "Scene lighting should record the true sun");
        Require(Near(night.directionalDirection, toSun), "A full moon should shine from opposite the sun");

        const auto atmosphere = render::BuildSceneAtmosphere(&world, night);
        const auto sky = std::find_if(atmosphere.begin(), atmosphere.end(), [](const auto &effect)
                                      { return effect.type == render::BasicPostProcessEffectType::PhysicalSky; });
        Require(sky != atmosphere.end(), "The scene should produce a sky packet");
        Require(Near(glm::vec3(sky->parameters[PLUTO_SKY_LANE_SUN]), toSun),
                "The sky must stay lit by the sun, not the moonlit directional light");

        sun->SetDirection(-Elevated(45.0f));
        const auto day = render::BuildSceneLighting(camera, &world);
        Require(!day.directionalIsMoon && day.directionalIntensity > 3.0f, "Scene lighting should be sunlit by day");
    }

    void TestComponentRoundTrip()
    {
        scene::PhysicalSkyComponent source;
        render::PhysicalSkySettings settings;
        settings.moonPlacement = render::MoonPlacement::Manual;
        settings.moonPhase = 0.3f;
        settings.moonCastsLight = false;
        settings.latitude = -33.0f;
        settings.lightPollutionColor = {0.2f, 0.4f, 0.9f};
        source.SetSettings(settings);

        scene::PhysicalSkyComponent loaded;
        loaded.Deserialize(source.Serialize());
        const auto &result = loaded.GetSettings();
        Require(result.moonPlacement == render::MoonPlacement::Manual && Near(result.moonPhase, 0.3f) &&
                    !result.moonCastsLight && Near(result.latitude, -33.0f) &&
                    Near(result.lightPollutionColor, settings.lightPollutionColor),
                "Night-sky settings should survive serialization");

        // Scenes saved before the night-sky settings keep their defaults.
        scene::PhysicalSkyComponent legacy;
        legacy.Deserialize({{"Moon Intensity", scene::PropertyType::Float, "0.4"}});
        Require(Near(legacy.GetMoonIntensity(), 0.4f) && legacy.GetSettings().moonCastsLight,
                "Older scenes should load with night-sky defaults");
    }
}

int main() try
{
    TestMoonPhases();
    TestCelestialFrame();
    TestCelestialLight();
    TestEnvironment();
    TestSceneIntegration();
    TestComponentRoundTrip();
    std::cout << "Physical sky model tests passed\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
