// Foliage wind (docs/FOLIAGE_WIND.md): the wind model's invariants, which FoliageWind.slang shares with the CPU
// reference, and the scene plumbing from WindComponent and foliage types to render commands.
#include "PlutoGE/render/FoliageWind.h"
#include "PlutoGE/render/FoliageWindLayout.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/SceneEnvironment.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/FoliageTypeAsset.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/WindComponent.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>

using namespace PlutoGE;

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    bool Near(float a, float b, float tolerance = 1.0e-4f) { return std::abs(a - b) <= tolerance; }

    render::SceneWindSettings Breeze(float speed = 8.0f)
    {
        render::SceneWindSettings wind;
        wind.direction = render::SceneWindDirectionFromDegrees(0.0f);
        wind.speed = speed;
        wind.gustiness = 0.5f;
        wind.gustSize = 30.0f;
        return wind;
    }

    render::FoliageWindResponse Grass()
    {
        return render::FoliageWindPresetResponse(render::FoliageWindPreset::Grass, 0.8f);
    }

    // Calm air, disabled types and fully stiff, flutterless plants bind all-zero constants and never move, so a
    // scene without wind renders exactly as it did before wind existed.
    void TestStillCases()
    {
        const auto grass = Grass();
        Require(render::PackFoliageWind({}, grass, 1.0f, 0.9f) == render::FoliageWindParameters{},
                "Calm air should pack zero constants");
        Require(render::PackFoliageWind(Breeze(), render::FoliageWindResponse{}, 1.0f, 0.9f) == render::FoliageWindParameters{},
                "A type without wind should pack zero constants");
        auto rigid = grass;
        rigid.stiffness = 1.0f;
        rigid.flutter = 0.0f;
        Require(!render::FoliageWindAnimates(Breeze(), rigid), "A rigid, flutterless type should not animate");
        Require(render::FoliageWindOffset({}, glm::mat4(1.0f), {0.0f, 1.0f, 0.0f}, 3.0f) == glm::vec3(0.0f),
                "Inactive parameters should not displace vertices");
        Require(render::FoliageWindMaxDisplacement(render::FoliageWindResponse{}) == 0.0f,
                "A type without wind needs no bounds padding");

        const render::FoliageWindFrame frame{Breeze(), 2.0f, 1.9f};
        Require(frame.Animates(grass) && frame.Parameters(grass) == render::PackFoliageWind(Breeze(), grass, 2.0f, 1.9f),
                "The frame state should pack what PackFoliageWind packs");
    }

    // Roots stay planted and the sway grows with height.
    void TestAnchoring()
    {
        auto tree = render::FoliageWindPresetResponse(render::FoliageWindPreset::Tree, 10.0f);
        const auto wind = render::PackFoliageWind(Breeze(12.0f), tree, 5.0f, 4.9f);
        const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(4.0f, 2.0f, -7.0f));
        for (float time = 0.0f; time < 10.0f; time += 0.37f)
        {
            Require(render::FoliageWindOffset(wind, model, {0.3f, 0.0f, 0.1f}, time) == glm::vec3(0.0f),
                    "The base of a trunk should not move");
            Require(render::FoliageWindOffset(wind, model, {0.0f, tree.pivotHeight, 0.0f}, time) == glm::vec3(0.0f),
                    "Geometry at the pivot height should not move");
        }
        float low = 0.0f, high = 0.0f;
        for (float time = 0.0f; time < 20.0f; time += 0.11f)
        {
            low += glm::length(render::FoliageWindOffset(wind, model, {0.0f, 4.0f, 0.0f}, time));
            high += glm::length(render::FoliageWindOffset(wind, model, {0.0f, 9.0f, 0.0f}, time));
        }
        Require(high > low * 2.0f, "The crown should sway further than the trunk");
    }

    // The packed bound holds for any wind the model accepts, every position, scale and time: culling pads bounds
    // with it without knowing the scene's wind.
    void TestDisplacementBound()
    {
        std::mt19937 random(7);
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        for (int trial = 0; trial < 400; ++trial)
        {
            render::SceneWindSettings wind;
            wind.direction = render::SceneWindDirectionFromDegrees(unit(random) * 360.0f);
            wind.speed = unit(random) * 60.0f; // beyond the clamp
            wind.gustiness = unit(random);
            wind.gustSize = 2.0f + unit(random) * 100.0f;
            render::FoliageWindResponse response;
            response.enabled = true;
            response.stiffness = unit(random);
            response.flutter = unit(random);
            response.bendHeight = 0.1f + unit(random) * 12.0f;
            response.pivotHeight = unit(random) * 0.3f * response.bendHeight;
            const float scale = 0.3f + unit(random) * 3.0f;
            const glm::mat4 model = glm::scale(
                glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(unit(random) * 500.0f, 0.0f, unit(random) * 500.0f)),
                            unit(random) * 6.0f, glm::vec3(0.0f, 1.0f, 0.0f)),
                glm::vec3(scale));
            const float time = unit(random) * 1000.0f;
            const auto packed = render::PackFoliageWind(wind, response, time, time - 0.016f);
            const float bound = render::FoliageWindMaxDisplacement(response) * scale;
            for (int sample = 0; sample < 16; ++sample)
            {
                const glm::vec3 local((unit(random) - 0.5f) * 2.0f, unit(random) * response.bendHeight * 2.5f,
                                      (unit(random) - 0.5f) * 2.0f);
                const float offset = glm::length(render::FoliageWindOffset(packed, model, local, time));
                Require(std::isfinite(offset) && offset <= bound * 1.0001f + 1.0e-6f,
                        "A vertex moved beyond FoliageWindMaxDisplacement");
            }
        }
    }

    // Plants lean downwind on average, softer plants further; gusts stay in range and roll downwind.
    void TestResponse()
    {
        const auto wind = Breeze(10.0f);
        const auto measure = [&](float stiffness)
        {
            auto response = Grass();
            response.stiffness = stiffness;
            response.flutter = 0.0f;
            const auto packed = render::PackFoliageWind(wind, response, 0.0f, 0.0f);
            glm::vec3 sum(0.0f);
            for (float time = 0.0f; time < 30.0f; time += 0.05f)
                sum += render::FoliageWindOffset(packed, glm::mat4(1.0f), {0.0f, 0.8f, 0.0f}, time);
            return sum / 600.0f;
        };
        const auto soft = measure(0.1f), stiff = measure(0.7f);
        Require(soft.x > 0.0f && std::abs(soft.z) < soft.x * 0.05f, "Grass should lean downwind (+X)");
        Require(soft.y < 0.0f, "Leaning blades should drop rather than stretch");
        Require(soft.x > stiff.x * 2.0f, "Softer plants should lean further");

        for (float x = -50.0f; x < 50.0f; x += 3.7f)
        {
            const float gust = render::SceneWindGust(wind, {x, x * 0.3f}, x * 0.2f);
            Require(gust >= 1.0f - wind.gustiness - 1.0e-5f && gust <= 1.0f + wind.gustiness + 1.0e-5f,
                    "Gusts should stay within the gustiness range");
            Require(Near(render::SceneWindGust(wind, {x, 4.0f}, 0.0f),
                         render::SceneWindGust(wind, {x + wind.speed * 2.5f, 4.0f}, 2.5f), 1.0e-3f),
                    "Gust fronts should travel downwind at the wind speed");
        }
        const glm::vec3 velocity = render::SceneWindVelocity(wind, {3.0f, 0.0f, 1.0f}, 2.0f);
        Require(velocity.x > 0.0f && Near(velocity.y, 0.0f) && Near(velocity.z, 0.0f, 1.0e-3f),
                "CPU consumers should sample the same downwind field");
        Require(Near(render::SceneWindStrength(Breeze(80.0f)), float(PLUTO_WIND_MAX_STRENGTH)),
                "Strength should be clamped");
    }

    void TestWindComponent()
    {
        scene::WindComponent source;
        source.SetDirectionDegrees(-90.0f);
        source.SetSpeed(12.5f);
        source.SetGustiness(0.8f);
        source.SetGustSize(60.0f);
        Require(Near(source.GetDirectionDegrees(), 270.0f), "Directions should wrap into [0, 360)");

        scene::WindComponent loaded;
        loaded.Deserialize(source.Serialize());
        Require(Near(loaded.GetDirectionDegrees(), 270.0f) && Near(loaded.GetSpeed(), 12.5f) &&
                    Near(loaded.GetGustiness(), 0.8f) && Near(loaded.GetGustSize(), 60.0f),
                "Wind settings should survive serialization");
        const auto settings = loaded.GetSettings();
        Require(Near(settings.direction.x, 0.0f, 1.0e-5f) && Near(settings.direction.y, -1.0f, 1.0e-5f),
                "270 degrees should blow toward -Z");

        scene::WindComponent clamped;
        clamped.Deserialize({{"Speed", scene::PropertyType::Float, "500"},
                             {"Gustiness", scene::PropertyType::Float, "not a number"},
                             {"Gust Size", scene::PropertyType::Float, "0"}});
        Require(Near(clamped.GetSpeed(), scene::WindComponent::MaxSpeed) && Near(clamped.GetGustiness(), 0.45f) &&
                    Near(clamped.GetGustSize(), scene::WindComponent::MinGustSize),
                "Out-of-range and malformed values should be clamped or ignored");
    }

    // The first enabled wind on an active entity drives the scene; the cached lookup follows components added and
    // removed after it was first read.
    void TestSceneExtraction()
    {
        scene::Scene world;
        const render::CameraData camera{};
        Require(render::BuildSceneLighting(camera, &world).wind.Calm(), "A scene without wind should be calm");

        auto *first = world.AddEntity(std::make_unique<scene::Entity>());
        auto *wind = first->CreateComponent<scene::WindComponent>();
        wind->SetSpeed(9.0f);
        Require(Near(render::BuildSceneLighting(camera, &world).wind.speed, 9.0f),
                "Scene lighting should carry a wind added after the first lookup");

        auto *second = world.AddEntity(std::make_unique<scene::Entity>());
        second->CreateComponent<scene::WindComponent>()->SetSpeed(3.0f);
        Require(Near(render::BuildSceneWind(&world).speed, 9.0f), "The first wind should drive the scene");
        wind->SetEnabled(false);
        Require(Near(render::BuildSceneWind(&world).speed, 3.0f), "A disabled wind should be skipped");
        second->SetActive(false);
        Require(render::BuildSceneWind(&world).Calm(), "Wind on an inactive entity should be skipped");
    }

    render::MeshConfig BladeMesh()
    {
        render::MeshConfig config;
        for (const auto p : {glm::vec3(-0.05f, 0.0f, 0.0f), glm::vec3(0.05f, 0.0f, 0.0f), glm::vec3(0.0f, 0.8f, 0.0f)})
        {
            render::MeshVertexData vertex{};
            vertex.position = {p.x, p.y, p.z};
            vertex.normal = {0.0f, 0.0f, 1.0f};
            config.data.vertices.push_back(vertex);
        }
        config.data.indices = {0, 1, 2};
        render::Submesh submesh;
        submesh.indexCount = 3;
        submesh.bounds = {{0.0f, 0.4f, 0.0f}, 0.41f};
        config.submeshes = {submesh};
        return config;
    }

    void TestFoliageTypes()
    {
        scene::Scene world;
        auto *owner = world.AddEntity(std::make_unique<scene::Entity>());
        auto *foliage = owner->CreateComponent<scene::FoliageComponent>();
        foliage->AddType("Grass");
        foliage->AddType("Rock");
        auto wild = Grass();
        wild.stiffness = 3.0f; // sanitized
        wild.bendHeight = -1.0f;
        foliage->SetTypeWind(0, wild);
        Require(foliage->GetType(0)->asset.wind.enabled && Near(foliage->GetType(0)->asset.wind.stiffness, 1.0f) &&
                    Near(foliage->GetType(0)->asset.wind.bendHeight, 0.01f),
                "SetTypeWind should sanitize the response");
        foliage->SetTypeWind(0, Grass());

        scene::FoliageComponent restored;
        restored.Deserialize(foliage->Serialize());
        Require(restored.GetTypeCount() == 2 && restored.GetType(0)->asset.wind == Grass() &&
                    !restored.GetType(1)->asset.wind.enabled,
                "Per-type wind should survive scene serialization");

        // Scenes saved before wind have no Wind* keys and load still.
        auto legacy = foliage->Serialize();
        std::erase_if(legacy, [](const scene::Property &property) { return property.name.find(".Wind") != std::string::npos; });
        scene::FoliageComponent old;
        old.Deserialize(legacy);
        Require(!old.GetType(0)->asset.wind.enabled, "Older scenes should load with wind off");

        // Commands carry the response, and swaying clusters widen their bounds by the displacement bound.
        render::Mesh mesh(BladeMesh());
        render::Material material;
        foliage->GetType(0)->instances.push_back(scene::FoliageInstance{.id = 1, .scale = glm::vec3(2.0f)});
        foliage->GetType(1)->instances.push_back(scene::FoliageInstance{.id = 2, .scale = glm::vec3(2.0f)});
        foliage->SetTypeMeshAndMaterials(0, &mesh, {&material}, {});
        foliage->SetTypeMeshAndMaterials(1, &mesh, {&material}, {});
        const auto &commands = foliage->GetRenderCommands();
        const auto swaying = std::find_if(commands.begin(), commands.end(), [](const auto &c) { return c.foliageWind.enabled; });
        const auto still = std::find_if(commands.begin(), commands.end(), [](const auto &c) { return !c.foliageWind.enabled; });
        Require(swaying != commands.end() && still != commands.end(), "Both types should produce commands");
        Require(swaying->foliageWind == Grass(), "Commands should carry the type's response");
        Require(Near(swaying->worldBounds.radius - still->worldBounds.radius,
                     render::FoliageWindMaxDisplacement(Grass()) * 2.0f, 1.0e-3f),
                "Swaying clusters should widen their bounds by the scaled displacement bound");
    }

    void TestFoliageTypeAsset()
    {
        const auto directory = std::filesystem::temp_directory_path() / "PlutoGEFoliageWindTests";
        std::filesystem::create_directories(directory);
        const auto path = (directory / "Grass.plutofoliage").string();
        scene::FoliageTypeAsset asset;
        asset.wind = render::FoliageWindPresetResponse(render::FoliageWindPreset::Tree, 6.0f);
        std::string error;
        Require(scene::SaveFoliageTypeAsset(path, asset, &error), error);
        scene::FoliageTypeAsset loaded;
        Require(scene::LoadFoliageTypeAsset(path, loaded, &error), error);
        Require(loaded.wind == asset.wind, "Wind should survive the foliage type asset");

        // Version 1 files written before wind load with it off.
        {
            std::ofstream legacy(path, std::ios::trunc);
            legacy << "PLUTOFOLIAGE\t1\nCELL_SIZE\t16\n";
        }
        scene::FoliageTypeAsset old;
        old.wind.enabled = true;
        Require(scene::LoadFoliageTypeAsset(path, old, &error), error);
        Require(!old.wind.enabled && Near(old.cellSize, 16.0f), "Older foliage assets should load with wind off");
        std::filesystem::remove_all(directory);
    }
}

int main()
try
{
    TestStillCases();
    TestAnchoring();
    TestDisplacementBound();
    TestResponse();
    TestWindComponent();
    TestSceneExtraction();
    TestFoliageTypes();
    TestFoliageTypeAsset();
    std::cout << "Foliage wind tests passed\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
