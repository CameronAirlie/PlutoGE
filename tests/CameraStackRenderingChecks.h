#pragma once
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/RhiCameraStack.h"
#include "PlutoGE/render/RhiSceneRenderer.h"
#include "PlutoGE/render/SceneEnvironment.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include <array>
#include <optional>
#include <span>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

// An overlay triangle placed behind full-screen base geometry must still be
// composited on top, exactly where it rasterizes alone, leaving the rest of
// the base image untouched.
template <class Device>
void CheckCameraStackComposite(Device &device, const PlutoGE::render::BasicRendererShaderPackage &shaders)
{
    using namespace PlutoGE::render;
    const auto require = [](bool ok, const std::string &message) {
        if (!ok)
            throw std::runtime_error(message);
    };
    const auto makeMesh = [](std::vector<std::array<float, 2>> corners, float depth) {
        MeshConfig config;
        for (const auto &corner : corners)
            config.data.vertices.push_back({{corner[0], corner[1], depth}, {0, 0, 1}, {0, 0}, {1, 0, 0, 1}});
        for (std::uint32_t index = 0; index < config.data.vertices.size(); ++index)
            config.data.indices.push_back(index);
        return std::make_unique<Mesh>(config);
    };
    // Reversed depth: the overlay (0.2) is farther away than the base (0.5).
    auto baseMesh = makeMesh({{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}}, 0.5f);
    auto overlayMesh = makeMesh({{-0.9f, 0.1f}, {0.9f, 0.1f}, {-0.9f, 0.9f}}, 0.2f);
    Material red({.emission = {1, 0, 0}});
    Material green({.emission = {0, 1, 0}});
    const std::array baseCommands{RenderCommand{.material = &red, .mesh = baseMesh.get()}};
    const std::array overlayCommands{RenderCommand{.material = &green, .mesh = overlayMesh.get()}};
    const CameraData camera{.view = glm::mat4(1), .projection = glm::mat4(1)};
    BasicLighting lighting;
    lighting.ambientIntensity = lighting.directionalIntensity = 0;
    constexpr std::uint32_t size = 64;

    // Reference: where the overlay rasterizes when rendered on its own.
    RhiSceneRenderer reference;
    require(reference.Initialize(device, shaders), "Camera stack reference renderer initialization failed");
    require(reference.Render(size, size, camera, lighting, overlayCommands, overlayCommands),
            "Camera stack reference render failed");
    const auto overlayAlone = device.ReadTextureRgba8(reference.GetColorTexture());

    RhiSceneRenderer base;
    require(base.Initialize(device, shaders), "Camera stack base renderer initialization failed");
    require(base.Render(size, size, camera, lighting, baseCommands, baseCommands), "Camera stack base render failed");
    RhiCameraStackCompositor compositor;
    require(compositor.Composite(device, base.GetColorTexture(), size, size, {}, {}, nullptr),
            "An empty camera stack must be a no-op");
    const auto baseAlone = device.ReadTextureRgba8(base.GetColorTexture());

    const std::array overlays{CameraView{.cameraData = camera, .commands = overlayCommands}};
    require(compositor.Composite(device, base.GetColorTexture(), size, size, overlays, {}, nullptr),
            "Camera stack composite failed");
    const auto stacked = device.ReadTextureRgba8(base.GetColorTexture());

    const auto channel = [](const std::vector<std::byte> &pixels, std::size_t pixel, std::size_t component) {
        return std::to_integer<int>(pixels[pixel * 4 + component]);
    };
    const auto isGreen = [&](const auto &pixels, std::size_t pixel) {
        return channel(pixels, pixel, 1) > 128 && channel(pixels, pixel, 0) < 64;
    };
    const auto isRed = [&](const auto &pixels, std::size_t pixel) {
        return channel(pixels, pixel, 0) > 128 && channel(pixels, pixel, 1) < 64;
    };
    std::size_t overlayPixels = 0;
    for (std::size_t pixel = 0; pixel < size * size; ++pixel)
    {
        require(isRed(baseAlone, pixel), "The base camera must cover the whole target");
        if (isGreen(overlayAlone, pixel))
        {
            ++overlayPixels;
            require(isGreen(stacked, pixel), "Overlay pixel " + std::to_string(pixel) + " was hidden by base depth");
        }
        else
            require(isRed(stacked, pixel), "Base pixel " + std::to_string(pixel) + " was overwritten outside overlay coverage");
    }
    // The triangle covers roughly a fifth of the target, all in one half.
    require(overlayPixels > size * size / 8 && overlayPixels < size * size / 3,
            "Overlay coverage was not the expected triangle");
    std::cout << "Camera stack composite placed " << overlayPixels << " overlay pixels over the base camera" << std::endl;

    // The overlay sees a receiver, but a world mesh excluded from its visible
    // commands must still cast onto it. A bright red caster also makes any
    // accidental leak into the overlay's color pass obvious.
    auto receiverMesh = makeMesh({{-2, -2}, {2, -2}, {2, 2}, {-2, -2}, {2, 2}, {-2, 2}}, -5);
    auto casterMesh = makeMesh({{-.6f, -.6f}, {.6f, -.6f}, {.6f, .6f},
                               {-.6f, -.6f}, {.6f, .6f}, {-.6f, .6f}}, -3);
    Material white({.color = {1, 1, 1, 1}});
    const std::array receiverCommands{RenderCommand{.material = &white, .mesh = receiverMesh.get(), .castsShadow = false}};
    std::array sceneCommands{receiverCommands[0], RenderCommand{.material = &red, .mesh = casterMesh.get()}};
    const Camera overlayCamera({.nearPlane = .1f, .farPlane = 20,
                                .projection = CameraProjection::Orthographic, .orthographicHeight = 4});
    const auto shadowCamera = overlayCamera.GetCameraDataForTransform(glm::mat4(1), size, size);
    PlutoGE::scene::Scene shadowScene;
    auto *sunEntity = shadowScene.AddEntity(std::make_unique<PlutoGE::scene::Entity>());
    auto &sun = sunEntity->CreateComponent<PlutoGE::scene::LightComponent>()->GetLight();
    sun.type = PlutoGE::scene::LightType::Directional;
    sun.direction = {0, 0, -1};
    sun.castsShadows = true;
    sun.directionalShadowSettings.cascadeCount = 1;
    sun.directionalShadowSettings.resolution = 512;
    sun.directionalShadowSettings.maxDistance = 20;
    const std::array shadowOverlays{CameraView{.cameraData = shadowCamera, .commands = receiverCommands,
                                              .shadowCommands = sceneCommands}};
    const auto drawOverlay = [&] {
        require(compositor.Composite(device, base.GetColorTexture(), size, size, shadowOverlays, {}, &shadowScene),
                "Shadow-receiving overlay composite failed");
        return device.ReadTextureRgba8(base.GetColorTexture());
    };
    sceneCommands[1].castsShadow = false;
    const auto lit = drawOverlay();
    sceneCommands[1].castsShadow = true;
    const auto shadowed = drawOverlay();
    const auto center = (size / 2) * size + size / 2;
    require(channel(lit, center, 1) > 40 && channel(shadowed, center, 1) + 30 < channel(lit, center, 1),
            "World geometry excluded from the overlay did not shadow its receiver");
    require(channel(shadowed, center, 0) < channel(lit, center, 0) - 30,
            "Shadow-only caster leaked into overlay color");
    const auto outside = (size / 8) * size + size / 8;
    require(std::abs(channel(shadowed, outside, 1) - channel(lit, outside, 1)) <= 3,
            "World shadow changed pixels outside its footprint");
    std::cout << "Overlay receives shadows from hidden world casters" << std::endl;
}

// A view given its own light list must ignore every other scene light: the
// surface is lit by the scene's point light only when that light is supplied.
template <class Device>
void CheckCameraLightFiltering(Device &device, const PlutoGE::render::BasicRendererShaderPackage &shaders)
{
    using namespace PlutoGE;
    using namespace PlutoGE::render;
    const auto require = [](bool ok, const std::string &message) {
        if (!ok)
            throw std::runtime_error(message);
    };
    MeshConfig config;
    for (const auto &corner : std::array<std::array<float, 2>, 6>{{{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}}})
        config.data.vertices.push_back({{corner[0], corner[1], 0.5f}, {0, 0, 1}, {0, 0}, {1, 0, 0, 1}});
    for (std::uint32_t index = 0; index < 6; ++index)
        config.data.indices.push_back(index);
    auto surface = std::make_unique<Mesh>(config);
    Material white({.color = {1, 1, 1, 1}});
    const std::array commands{RenderCommand{.material = &white, .mesh = surface.get()}};
    const CameraData camera{.view = glm::mat4(1), .projection = glm::mat4(1)};

    scene::Scene scene;
    auto *lamp = scene.AddEntity(std::make_unique<scene::Entity>());
    lamp->SetPosition({0, 0, 1.5f});
    auto *light = lamp->CreateComponent<scene::LightComponent>();
    light->SetLightType(scene::LightType::Point);
    light->SetIntensity(40.0f);
    light->Update(0.0f);
    require(scene.GetLights().size() == 1, "Light filtering fixture needs one scene light");

    constexpr std::uint32_t size = 32;
    RhiSceneRenderer renderer;
    require(renderer.Initialize(device, shaders), "Light filtering renderer initialization failed");
    const auto brightness = [&](std::optional<std::span<scene::Light *const>> lights) {
        const auto lighting = lights ? BuildSceneLighting(camera, &scene, *lights) : BuildSceneLighting(camera, &scene);
        require(renderer.Render(size, size, camera, lighting, commands, commands, {}, {}, {},
                                PostProcessDebugView::None, true, &scene, lights),
                "Light filtering render failed");
        const auto pixels = device.ReadTextureRgba8(renderer.GetColorTexture());
        const auto centre = (size / 2 * size + size / 2) * 4;
        return std::to_integer<int>(pixels[centre]) + std::to_integer<int>(pixels[centre + 1]) +
               std::to_integer<int>(pixels[centre + 2]);
    };
    const int allLights = brightness(std::nullopt);
    const auto sceneLights = scene.GetLights();
    const int suppliedLight = brightness(std::span<scene::Light *const>(sceneLights));
    const int noLights = brightness(std::span<scene::Light *const>{});
    require(allLights > 60, "The scene light did not illuminate the reference view: " + std::to_string(allLights));
    require(std::abs(suppliedLight - allLights) <= 3, "Supplying the scene's own light changed the result");
    require(noLights < allLights / 4, "A view without lights was still lit by the scene: " + std::to_string(noLights) +
                                          " vs " + std::to_string(allLights));
    std::cout << "Camera light filtering: lit " << allLights << ", filtered out " << noLights << std::endl;
}
