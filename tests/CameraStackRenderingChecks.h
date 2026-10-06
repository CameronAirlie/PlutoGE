#pragma once
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/RhiCameraStack.h"
#include "PlutoGE/render/RhiSceneRenderer.h"
#include "PlutoGE/render/SceneEnvironment.h"
#include "PlutoGE/render/postprocess/TAAEffect.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/CameraTagFilter.h"
#include "PlutoGE/scene/components/ParticleSystemComponent.h"
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
void CheckCameraStackComposite(Device &device, const PlutoGE::render::BasicRendererShaderPackage &shaders,
    const std::function<std::vector<std::byte>(PlutoGE::render::rhi::TextureHandle)> &pixelReader = {})
{
    using namespace PlutoGE::render;
    const auto readPixels = [&](rhi::TextureHandle texture)
    {
        if (pixelReader)
            return pixelReader(texture);
        if constexpr (requires { device.ReadTextureRgba8(texture); })
            return device.ReadTextureRgba8(texture);
        else
            throw std::runtime_error("Camera stack checks require a pixel reader");
    };
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
    const auto overlayAlone = readPixels(reference.GetColorTexture());

    RhiSceneRenderer base;
    require(base.Initialize(device, shaders), "Camera stack base renderer initialization failed");
    require(base.Render(size, size, camera, lighting, baseCommands, baseCommands), "Camera stack base render failed");
    RhiCameraStackCompositor compositor;
    require(compositor.Composite(device, base.GetColorTexture(), size, size, {}, {}, nullptr),
            "An empty camera stack must be a no-op");
    const auto baseAlone = readPixels(base.GetColorTexture());

    const std::array overlays{CameraView{.cameraData = camera, .commands = overlayCommands}};
    require(compositor.Composite(device, base.GetColorTexture(), size, size, overlays, {}, nullptr),
            "Camera stack composite failed");
    const auto stacked = readPixels(base.GetColorTexture());

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

    // The production stack resolves once, after HDR composition. A silhouette
    // must contain blended red/green coverage even though the base has no edge.
    TAAEffect taa;
    const std::array<IPostProcessEffect *, 1> effects{&taa};
    auto temporalOverlays = overlays;
    temporalOverlays[0].historyKey = overlayMesh.get();
    temporalOverlays[0].cameraData.projection[0][0] = 0.85f;
    temporalOverlays[0].cameraData.projection[1][1] = 0.75f;
    const BasicRenderer::BeforeTemporalResolve compose = [&](BasicRenderer &renderer, glm::vec2 jitter)
    {
        require(compositor.CompositeBeforeTemporalResolve(device, renderer, jitter, temporalOverlays, {}, nullptr),
                "HDR camera stack composition failed");
    };
    const auto drawStack = [&](std::uint32_t width, std::uint32_t height, bool aa)
    {
        require(base.Render(width, height, camera, lighting, baseCommands, baseCommands,
                            aa ? std::span<IPostProcessEffect *const>(effects) : std::span<IPostProcessEffect *const>{},
                            {}, {}, PostProcessDebugView::None, true, nullptr, std::nullopt, compose),
                "Temporal camera stack render failed");
        return readPixels(base.GetColorTexture());
    };
    const auto partialCoverage = [&](const auto &pixels)
    {
        std::size_t count = 0;
        for (std::size_t pixel = 0; pixel < pixels.size() / 4; ++pixel)
            count += channel(pixels, pixel, 0) > 8 && channel(pixels, pixel, 0) < 247 &&
                     channel(pixels, pixel, 1) > 8 && channel(pixels, pixel, 1) < 247;
        return count;
    };
    const auto unfilteredCoverage = partialCoverage(drawStack(size, size, false));
    std::vector<std::byte> resolved;
    for (int frame = 0; frame < 32; ++frame)
        resolved = drawStack(size, size, true);
    const auto filteredCoverage = partialCoverage(resolved);
    require(filteredCoverage > unfilteredCoverage + 10,
            "TAA did not resolve overlay silhouette coverage after composition: " +
            std::to_string(filteredCoverage) + " vs " + std::to_string(unfilteredCoverage));
    std::cout << "Shared camera-stack TAA: " << filteredCoverage << " fractional edge pixels" << std::endl;
    // Only the overlay camera moves; its own projection/reprojection must be
    // used rather than the stationary base camera's matrices.
    for (int frame = 0; frame < 10; ++frame)
    {
        temporalOverlays[0].cameraData.view[3][0] += 0.045f;
        resolved = drawStack(size, size, true);
    }
    const auto movedReference = drawStack(size, size, false);
    std::size_t ghostPixels = 0;
    for (int y = 2; y < static_cast<int>(size) - 2; ++y)
    for (int x = 2; x < static_cast<int>(size) - 2; ++x)
    {
        bool nearOverlay = false;
        for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx)
            nearOverlay |= channel(movedReference, (y + dy) * size + x + dx, 1) > 32;
        ghostPixels += !nearOverlay && channel(resolved, y * size + x, 1) > 32;
    }
    require(ghostPixels == 0, "Moving overlay left temporal ghosts outside its silhouette");
    for (int frame = 0; frame < 16; ++frame)
        resolved = drawStack(size, size, true);
    // Shared camera history must not retain an overlay after it is disabled.
    require(base.Render(size, size, camera, lighting, baseCommands, baseCommands, effects),
            "Camera stack removal render failed");
    const auto removed = readPixels(base.GetColorTexture());
    for (std::size_t pixel = 0; pixel < size * size; ++pixel)
        require(channel(removed, pixel, 1) < 10, "TAA retained a removed camera layer");
    for (int frame = 0; frame < 24; ++frame)
        resolved = drawStack(96, 48, true);
    require(partialCoverage(resolved) > 10, "Resizing the camera stack lost temporal edge coverage");
    require(base.Render(size, size, camera, lighting, baseCommands, baseCommands),
            "Camera stack base size restoration failed");

    // A weapon flash has no mesh depth and is excluded from the base camera.
    // Its premultiplied colour must survive the HDR overlay boundary anyway.
    PlutoGE::scene::Scene particleScene;
    auto flashEntity = std::make_unique<PlutoGE::scene::Entity>();
    flashEntity->AddTag("Weapon");
    auto *flash = flashEntity->CreateComponent<PlutoGE::scene::ParticleSystemComponent>();
    flash->SetPlayOnAwake(false);
    flash->SetEmissionRateOverTime(0);
    flash->SetStartSpeed(0);
    flash->SetStartSize(.45f);
    flash->SetStartLifetime(1);
    flash->SetStartColor({0, 1, 0, .5f});
    flash->SetRenderShape(PlutoGE::assets::ParticleRenderShape::Quad);
    particleScene.AddEntity(std::move(flashEntity));
    flash->EmitAt({0, 0, .2f}, 1);
    PlutoGE::scene::CameraTagFilter worldTags({}, {"Weapon"});
    PlutoGE::scene::CameraTagFilter weaponTags({"Weapon"}, {});
    auto worldCamera = camera;
    worldCamera.tagFilter = &worldTags;
    auto weaponCamera = camera;
    weaponCamera.tagFilter = &weaponTags;
    const std::array flashOverlay{CameraView{.cameraData = weaponCamera}};
    const BasicRenderer::BeforeTemporalResolve composeFlash = [&](BasicRenderer &renderer, glm::vec2 jitter)
    {
        require(compositor.CompositeBeforeTemporalResolve(device, renderer, jitter, flashOverlay, {}, &particleScene),
                "Particle camera overlay composition failed");
    };
    require(base.Render(size, size, worldCamera, lighting, baseCommands, baseCommands, {}, {}, {},
                        PostProcessDebugView::None, true, &particleScene), "Particle exclusion render failed");
    auto excludedFlash = readPixels(base.GetColorTexture());
    const auto centre = (size / 2) * size + size / 2;
    require(isRed(excludedFlash, centre), "Weapon particles leaked into the base camera");
    require(base.Render(size, size, worldCamera, lighting, baseCommands, baseCommands, {}, {}, {},
                        PostProcessDebugView::None, true, &particleScene, std::nullopt, composeFlash),
            "Muzzle flash overlay render failed");
    const auto flashed = readPixels(base.GetColorTexture());
    require(channel(flashed, centre, 1) > 60 && channel(flashed, centre, 0) > 60,
            "Transparent overlay flash was discarded or replaced the world background");
    require(isRed(flashed, 0), "Transparent camera clear colour leaked outside muzzle flash coverage");
    flash->Clear();
    require(base.Render(size, size, worldCamera, lighting, baseCommands, baseCommands, {}, {}, {},
                        PostProcessDebugView::None, true, &particleScene, std::nullopt, composeFlash),
            "Expired muzzle flash overlay render failed");
    require(isRed(readPixels(base.GetColorTexture()), centre), "Cleared muzzle flash remained visible");

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
        return readPixels(base.GetColorTexture());
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
