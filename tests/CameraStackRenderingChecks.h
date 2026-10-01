#pragma once
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/RhiCameraStack.h"
#include "PlutoGE/render/RhiSceneRenderer.h"
#include <array>
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
}
