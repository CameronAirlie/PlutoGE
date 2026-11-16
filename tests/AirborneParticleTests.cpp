// Airborne particles (docs/AIRBORNE_PARTICLES.md): camera volumes, view shaping (ParticleViewShaping.h), scattering and
// view-fade settings in particle assets, prewarming, and how much of the scene's wind a system takes up.
#include "ParticleViewShaping.h"

#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/ParticleSystemAsset.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/ParticleSystemComponent.h"
#include "PlutoGE/scene/components/WindComponent.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include <glm/gtc/matrix_transform.hpp>

using namespace PlutoGE;

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    bool Near(float a, float b, float tolerance = 1e-4f) { return std::abs(a - b) <= tolerance; }
    bool Near(const glm::vec3 &a, const glm::vec3 &b, float tolerance = 1e-4f) { return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec3(tolerance))); }

    void CheckCameraVolumeWrap()
    {
        using render::WrapIntoCameraVolume;
        const glm::vec3 size{10.0f, 4.0f, 20.0f};
        const glm::vec3 camera{3.0f, 1.0f, -7.0f};
        // Inside the box: unchanged, so the field is still while the camera moves within a tile.
        Require(Near(WrapIntoCameraVolume(camera + glm::vec3(4.9f, -1.9f, 9.9f), camera, size), camera + glm::vec3(4.9f, -1.9f, 9.9f)),
                "A particle within the camera's box stays where it is");
        // Beyond a face: the copy one period over, inside the box.
        const glm::vec3 wrapped = WrapIntoCameraVolume(camera + glm::vec3(5.5f, 0.0f, -31.0f), camera, size);
        Require(Near(wrapped, camera + glm::vec3(-4.5f, 0.0f, 9.0f)), "A particle beyond a face reappears at the opposite face");
        // Any particle, however far it drifted, lands within half a period of the camera.
        for (float x : {-1234.5f, -3.0f, 0.0f, 77.7f, 9001.0f})
        {
            const glm::vec3 offset = WrapIntoCameraVolume({x, x * 0.37f, -x}, camera, size) - camera;
            Require(glm::all(glm::lessThanEqual(glm::abs(offset), size * 0.5f + 1e-3f)), "Wrapped particles stay inside the box");
        }
        // Moving the camera does not move a particle that stays inside the box.
        const glm::vec3 particle{1.0f, 1.0f, 1.0f};
        Require(Near(WrapIntoCameraVolume(particle, {0, 0, 0}, size), WrapIntoCameraVolume(particle, {2, 0.5f, 3}, size)),
                "Particles are world-stable while the camera moves");
    }

    void CheckFades()
    {
        using render::CameraVolumeEdgeFade;
        const glm::vec3 size{10.0f, 10.0f, 10.0f};
        Require(Near(CameraVolumeEdgeFade({0, 0, 0}, size, 0.3f), 1.0f), "The volume's centre is opaque");
        Require(Near(CameraVolumeEdgeFade({3.4f, 0, 0}, size, 0.3f), 1.0f), "Inside the fade band particles are opaque");
        Require(Near(CameraVolumeEdgeFade({5.0f, 0, 0}, size, 0.3f), 0.0f), "Particles vanish at a face");
        Require(Near(CameraVolumeEdgeFade({0, 0, -5.0f}, size, 0.3f), 0.0f), "Every face fades");
        const float mid = CameraVolumeEdgeFade({4.25f, 0, 0}, size, 0.3f);
        Require(mid > 0.3f && mid < 0.7f, "The fade eases across its band");
        Require(Near(CameraVolumeEdgeFade({4.99f, 0, 0}, size, 0.0f), 1.0f), "No edge fade keeps the box hard-edged");

        using render::CameraNearFade;
        Require(Near(CameraNearFade(0.1f, 0.0f), 1.0f), "No near fade leaves close particles alone");
        Require(Near(CameraNearFade(0.2f, 0.6f), 0.0f) && Near(CameraNearFade(0.6f, 0.6f), 1.0f),
                "Near fade runs from half the distance to the distance");
        Require(CameraNearFade(0.45f, 0.6f) > 0.0f && CameraNearFade(0.45f, 0.6f) < 1.0f, "Near fade is gradual");

        using render::ParticleWeatherVisibility;
        Require(Near(ParticleWeatherVisibility({0.0f, 0.0f}, 1.0f), 1.0f), "Dry weather leaves particles");
        Require(Near(ParticleWeatherVisibility({1.0f, 0.2f}, 1.0f), 0.0f), "Soaked ground removes fully faded particles");
        Require(Near(ParticleWeatherVisibility({0.1f, 0.5f}, 0.8f), 0.6f), "The wetter of rain and ground thins particles");
        Require(Near(ParticleWeatherVisibility({1.0f, 1.0f}, 0.0f), 1.0f), "No wet-weather fade ignores the weather");
    }

    void CheckScreenFit()
    {
        using render::FitParticleToScreen;
        using render::ParticlePixelsPerMetre;
        // 90 degree vertical field on a 1000 px tall view: 500 px per metre at 1 m.
        const glm::mat4 perspective = glm::perspectiveRH_ZO(glm::radians(90.0f), 1.0f, 0.1f, 1000.0f);
        Require(Near(ParticlePixelsPerMetre(perspective, 1000.0f, 1.0f), 500.0f, 0.01f) &&
                    Near(ParticlePixelsPerMetre(perspective, 1000.0f, 10.0f), 50.0f, 0.01f),
                "Perspective pixels per metre fall with depth");
        const glm::mat4 orthographic = glm::orthoRH_ZO(-5.0f, 5.0f, -5.0f, 5.0f, 0.1f, 100.0f);
        Require(Near(ParticlePixelsPerMetre(orthographic, 1000.0f, 1.0f), ParticlePixelsPerMetre(orthographic, 1000.0f, 50.0f)),
                "Orthographic pixels per metre ignore depth");

        // A 2 mm speck at 10 m covers 0.1 px; held at 1.5 px it keeps its coverage.
        const auto fit = FitParticleToScreen(0.002f, 50.0f, 1.5f);
        Require(Near(fit.size * 50.0f, 1.5f, 1e-4f), "A sub-pixel particle is drawn at the minimum width");
        Require(Near(fit.opacity * fit.size * fit.size, 0.002f * 0.002f, 1e-9f), "Its opacity keeps the area it covered");
        const auto large = FitParticleToScreen(0.1f, 50.0f, 1.5f);
        Require(Near(large.size, 0.1f) && Near(large.opacity, 1.0f), "A particle wider than the minimum is untouched");
        Require(Near(FitParticleToScreen(0.002f, 50.0f, 0.0f).size, 0.002f), "No minimum leaves particles their size");
    }

    void CheckAssetRoundTrip()
    {
        const auto folder = std::filesystem::temp_directory_path() / "PlutoGEAirborneParticleTests";
        std::filesystem::create_directories(folder);
        assets::AssetManager manager;
        std::string error;

        assets::ParticleSystemAsset dust;
        dust.prewarm = true;
        dust.simulationSpace = assets::ParticleSimulationSpace::CameraVolume;
        dust.lighting = assets::ParticleLighting::Scattering;
        dust.scattering = {0.55f, 1.75f, 0.4f, false};
        dust.cameraVolume = {{24.0f, 12.0f, 18.0f}, 0.4f};
        dust.viewFade = {0.8f, 1.5f};
        dust.sceneWindDrag = 0.6f;
        dust.sceneWindScale = 0.3f;
        dust.wetWeatherFade = 0.9f;
        const auto path = (folder / "Dust.plutoparticles").string();
        Require(manager.SaveParticleSystemAsset(path, dust, &error), error);
        bool loaded = false;
        const auto reloaded = manager.LoadParticleSystemAsset(path, &loaded);
        Require(loaded, "The dust asset loads");
        Require(reloaded.prewarm && reloaded.simulationSpace == assets::ParticleSimulationSpace::CameraVolume,
                "Prewarm and the camera volume space survive a save");
        Require(reloaded.lighting == assets::ParticleLighting::Scattering && reloaded.scattering == dust.scattering,
                "Scattering lighting survives a save");
        Require(reloaded.cameraVolume == dust.cameraVolume && reloaded.viewFade == dust.viewFade,
                "Camera volume and view fades survive a save");
        Require(Near(reloaded.sceneWindScale, 0.3f) && Near(reloaded.wetWeatherFade, 0.9f),
                "Scene wind scale and wet-weather fade survive a save");

        // Files from before the lighting model was an enum keep their smoke lighting; out-of-range values clamp.
        const auto legacy = folder / "Legacy.plutoparticles";
        std::ofstream(legacy) << "ParticleSystemVersion=2\nSmokeLightingEnabled=true\nCameraVolumeSize=-4,0,1e9\n"
                                 "ScatteringAnisotropy=3\nMinScreenSize=-2\nWetWeatherFade=7\nSceneWindScale=nan\n";
        const auto old = manager.LoadParticleSystemAsset(legacy.string(), &loaded);
        Require(loaded && old.lighting == assets::ParticleLighting::Smoke, "Legacy Smoke Lighting Enabled loads as Smoke");
        Require(old.cameraVolume.size.x >= 0.1f && old.cameraVolume.size.z <= 10000.0f, "Volume sizes are clamped");
        Require(Near(old.scattering.anisotropy, 0.9f) && Near(old.viewFade.minScreenSize, 0.0f) &&
                    Near(old.wetWeatherFade, 1.0f) && Near(old.sceneWindScale, 1.0f),
                "Out-of-range and non-finite settings are sanitized");
        const auto overridden = folder / "Override.plutoparticles";
        std::ofstream(overridden) << "ParticleSystemVersion=2\nLighting=Scattering\nSmokeLightingEnabled=true\n";
        Require(manager.LoadParticleSystemAsset(overridden.string(), &loaded).lighting == assets::ParticleLighting::Scattering,
                "An explicit Lighting line wins over the legacy smoke flag");
        std::filesystem::remove_all(folder);
    }

    // Headless there is no render device, so a system simulates on the CPU only once asked to emit at a position.
    void UseCpuSimulation(scene::ParticleSystemComponent &system, const glm::vec3 &position)
    {
        system.EmitAt(position, 1);
        system.Stop(true);
    }

    int LiveParticles(const scene::ParticleSystemComponent &system)
    {
        int live = 0;
        for (const auto &particle : system.GetCpuParticles())
            live += particle.active && particle.age < particle.lifetime ? 1 : 0;
        return live;
    }

    void CheckCameraVolumeSpawnAndPrewarm()
    {
        scene::Scene scene;
        auto *entity = scene.AddEntity(std::make_unique<scene::Entity>());
        const glm::vec3 origin{100.0f, 5.0f, -50.0f};
        entity->SetPosition(origin);
        entity->SetRotation({0.0f, 45.0f, 30.0f});
        entity->SetScale({3.0f, 3.0f, 3.0f});
        auto *system = entity->CreateComponent<scene::ParticleSystemComponent>();
        assets::ParticleSystemAsset asset;
        asset.simulationSpace = assets::ParticleSimulationSpace::CameraVolume;
        asset.cameraVolume.size = {10.0f, 4.0f, 6.0f};
        asset.looping = true;
        asset.prewarm = true;
        asset.playOnAwake = false;
        asset.maxParticles = 4000;
        asset.emissionRateOverTime = 200.0f;
        asset.startLifetime = 5.0f;
        asset.startSpeed = 0.0f;
        asset.shape = assets::ParticleShape::Cone; // ignored by a camera volume
        system->ApplyParticleSystemAsset(asset);
        UseCpuSimulation(*system, origin);

        system->Play();
        const int live = LiveParticles(*system);
        Require(live > 800 && live <= 1000, "Prewarm fills a looping system to its steady state (" + std::to_string(live) + ")");
        // Spawned evenly through the world-axis box around the emitter, ignoring its rotation, scale and shape.
        glm::vec3 low{std::numeric_limits<float>::max()}, high{-std::numeric_limits<float>::max()};
        for (const auto &particle : system->GetCpuParticles())
        {
            if (!particle.active)
                continue;
            low = glm::min(low, particle.position - origin);
            high = glm::max(high, particle.position - origin);
        }
        Require(glm::all(glm::greaterThanEqual(low, -asset.cameraVolume.size * 0.5f - 1e-3f)) &&
                    glm::all(glm::lessThanEqual(high, asset.cameraVolume.size * 0.5f + 1e-3f)),
                "Camera volume particles spawn inside the axis-aligned volume");
        Require(glm::all(glm::greaterThan(high - low, asset.cameraVolume.size * 0.8f)), "They fill the whole volume");

        // A pause keeps the particles; resuming does not prewarm again.
        system->Pause();
        system->Play();
        Require(LiveParticles(*system) == live, "Resuming a paused system does not prewarm it again");

        // Without prewarm a system starts empty.
        asset.prewarm = false;
        system->ApplyParticleSystemAsset(asset);
        UseCpuSimulation(*system, origin);
        system->Play();
        Require(LiveParticles(*system) == 0, "A system without prewarm starts empty");
    }

    void CheckSceneWindScale()
    {
        scene::Scene scene;
        auto *windEntity = scene.AddEntity(std::make_unique<scene::Entity>());
        auto *wind = windEntity->CreateComponent<scene::WindComponent>();
        wind->SetDirectionDegrees(0.0f);
        wind->SetSpeed(6.0f);
        wind->SetGustiness(0.0f);
        auto *entity = scene.AddEntity(std::make_unique<scene::Entity>());
        auto *system = entity->CreateComponent<scene::ParticleSystemComponent>();
        assets::ParticleSystemAsset asset;
        asset.simulationSpace = assets::ParticleSimulationSpace::World;
        asset.playOnAwake = false;
        asset.emissionRateOverTime = 0.0f;
        asset.startSpeed = 0.0f;
        asset.startLifetime = 100.0f;
        asset.sceneWindDrag = 1000.0f; // takes up the air at once
        asset.sceneWindScale = 0.25f;
        system->ApplyParticleSystemAsset(asset);
        system->EmitAt({0.0f, 0.0f, 0.0f}, 4);
        system->Update(0.1f);
        for (const auto &particle : system->GetCpuParticles())
            if (particle.active)
                Require(Near(particle.velocity.x, 1.5f, 1e-3f) && Near(particle.velocity.z, 0.0f, 1e-3f),
                        "Particles take up the scaled wind: " + std::to_string(particle.velocity.x));
    }
}

int main()
try
{
    CheckCameraVolumeWrap();
    CheckFades();
    CheckScreenFit();
    CheckAssetRoundTrip();
    CheckCameraVolumeSpawnAndPrewarm();
    CheckSceneWindScale();
    std::cout << "AirborneParticleTests passed\n";
    return 0;
}
catch (const std::exception &exception)
{
    std::cerr << "AirborneParticleTests failed: " << exception.what() << '\n';
    return 1;
}
