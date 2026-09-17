#pragma once
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/RhiSceneRenderer.h"
#include "PlutoGE/render/SceneEnvironment.h"
#include "PlutoGE/render/postprocess/VoxelConeTracingEffect.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/PhysicalSkyComponent.h"
#include "PlutoGE/scene/components/VolumetricCloudComponent.h"
#include <array>
#include <stdexcept>

inline void CheckEnvironmentRegistry()
{
    using namespace PlutoGE;
    const auto require = [](bool ok) {
        if (!ok)
            throw std::runtime_error("Environment registry invalidation failed");
    };
    scene::Scene scene;
    require(scene.GetLights().empty() && scene.GetPhysicalSkyComponents().empty());
    auto *parent = scene.AddEntity(std::make_unique<scene::Entity>());
    auto *child = scene.AddEntity(std::make_unique<scene::Entity>(), parent);
    auto *light = child->CreateComponent<scene::LightComponent>();
    auto *sky = child->CreateComponent<scene::PhysicalSkyComponent>();
    auto *cloud = child->CreateComponent<scene::VolumetricCloudComponent>();
    require(scene.GetLights().size() == 1 && scene.GetPhysicalSkyComponents().front() == sky &&
            scene.GetVolumetricCloudComponents().front() == cloud);
    parent->SetActive(false);
    require(scene.GetLights().empty() && render::BuildSceneAtmosphere(&scene, {}).empty());
    parent->SetActive(true);
    light->SetEnabled(false);
    require(scene.GetLights().empty());
    light->SetEnabled(true);
    require(scene.GetLights().size() == 1);
    auto *other = scene.AddEntity(std::make_unique<scene::Entity>());
    auto *otherSky = other->CreateComponent<scene::PhysicalSkyComponent>();
    require(scene.GetPhysicalSkyComponents().front() == sky);
    child->SetParent(other);
    require(scene.GetPhysicalSkyComponents().front() == otherSky);
    other->RemoveComponent(otherSky);
    require(scene.GetPhysicalSkyComponents().size() == 1 && scene.GetPhysicalSkyComponents().front() == sky);
    child->RemoveComponent(cloud);
    require(scene.GetVolumetricCloudComponents().empty());
    scene.RemoveEntity(other);
    require(scene.GetPhysicalSkyComponents().empty() && scene.GetLights().empty());
}

template <class Device>
void CheckPreparationCache(Device &device, const PlutoGE::render::BasicRendererShaderPackage &shaders)
{
    using namespace PlutoGE::render;
    const auto require = [](bool ok, const char *message) {
        if (!ok)
            throw std::runtime_error(message);
    };
    CheckEnvironmentRegistry();
    MeshConfig config;
    config.data.vertices = {{{-.6f, -.6f, .5f}, {0, 0, 1}, {0, 0}, {1, 0, 0, 1}},
                            {{.6f, -.6f, .5f}, {0, 0, 1}, {1, 0}, {1, 0, 0, 1}},
                            {{0, .6f, .5f}, {0, 0, 1}, {.5f, 1}, {1, 0, 0, 1}}};
    config.data.indices = {0, 1, 2};
    auto mesh = std::make_unique<Mesh>(config);
    Material material({.emission = {1, 0, 0}});
    std::array commands{RenderCommand{.material = &material, .mesh = mesh.get()}};
    CameraData camera{.view = glm::mat4(1), .projection = glm::mat4(1)};
    BasicLighting lighting;
    lighting.ambientIntensity = lighting.directionalIntensity = 0;
    lighting.shadowsEnabled = true;
    lighting.shadowResolution = 128;
    RhiSceneRenderer renderer;
    require(renderer.Initialize(device, shaders), "Preparation renderer initialization failed");
    const auto render = [&] {
        require(renderer.Render(64, 64, camera, lighting, commands, commands), "Preparation render failed");
        return device.ReadTextureRgba8(renderer.GetColorTexture());
    };
    const auto red = render();
    require(render() == red && renderer.GetTimingStats().rebuiltDrawPackets == 0 &&
                renderer.GetTimingStats().reusedDrawPackets == 2,
            "Static packets were not reused");
    auto &mutableConfig = material.GetConfig();
    mutableConfig.emission = {0, 1, 0};
    const auto green = render();
    require(green != red && renderer.GetTimingStats().rebuiltDrawPackets == 2, "Material edit used stale packets");
    require(render() == green && renderer.GetTimingStats().rebuiltDrawPackets == 0, "Edited material did not settle");
    mutableConfig.emission = {0, 0, 1};
    require(render() != green, "Retained material reference edit was missed");
    commands[0].model[3].x = .4f;
    const auto moved = render();
    require(renderer.GetTimingStats().rebuiltDrawPackets == 2, "Transform change was missed");
    commands[0].previousModel = commands[0].model;
    render();
    require(renderer.GetTimingStats().rebuiltDrawPackets == 2, "Motion history change was missed");
    commands[0].worldBounds.radius = 3;
    render();
    require(renderer.GetTimingStats().rebuiltDrawPackets == 2, "Caster bounds change was missed");
    auto instances = std::make_shared<std::vector<glm::mat4>>(1, glm::mat4(1));
    commands[0].instanceModels = instances;
    const auto firstInstance = render();
    (*instances)[0][3].x = -.4f;
    require(render() != firstInstance && renderer.GetTimingStats().rebuiltDrawPackets == 2,
            "Mutable instance data was cached");
    commands[0].instanceModels.reset();
    render();
    render();
    renderer.InvalidateAssetCache();
    require(render() == moved && renderer.GetTimingStats().rebuiltDrawPackets == 2,
            "Asset invalidation did not rebuild packets");
    mesh.reset();
    mesh = std::make_unique<Mesh>(config);
    commands[0].mesh = mesh.get();
    render();
    require(renderer.GetTimingStats().meshUploadCount == 1 && renderer.GetTimingStats().rebuiltDrawPackets == 2,
            "Mesh lifetime invalidation was missed");
    VoxelConeTracingEffect gi;
    gi.ApplyParameters({{"Volume Size", PostProcessParameterType::Float, "4"},
                        {"World Cache", PostProcessParameterType::Bool, "false"},
                        {"Cascade Count", PostProcessParameterType::Int, "1"}});
    const std::array<IPostProcessEffect *, 1> effects{&gi};
    const auto renderGi = [&] {
        require(renderer.Render(64, 64, camera, lighting, commands, commands, effects), "GI preparation failed");
        device.ReadTextureRgba8(renderer.GetColorTexture());
    };
    renderGi();
    renderGi();
    require(renderer.GetTimingStats().rebuiltDrawPackets == 0 && renderer.GetTimingStats().reusedDrawPackets == 3,
            "GI packets were not reused");
    mutableConfig.emission = {1, 0, 0};
    renderGi();
    require(renderer.GetTimingStats().rebuiltDrawPackets == 3, "GI material changes were missed");
    commands[0].castsShadow = false;
    renderGi();
    require(renderer.GetTimingStats().shadowCandidateCount == 0, "Disabled caster survived in the cache");
    commands[0].castsShadow = true;
    renderGi();
    require(renderer.GetTimingStats().shadowCandidateCount == 1, "Re-enabled caster stayed excluded");
    require(renderer.Render(64, 64, camera, lighting, {}, {}), "Empty list render failed");
    require(renderer.GetDrawCount() == 0, "Removed commands survived in the cache");
}
