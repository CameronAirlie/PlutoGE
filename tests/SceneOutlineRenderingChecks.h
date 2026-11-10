#pragma once
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Outline.h"
#include "PlutoGE/render/RenderingSettings.h"
#include "PlutoGE/render/RhiSceneRenderer.h"
#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// The path OutlineComponent takes in built games and the editor's views: render commands owned by each layer's
// entities are outlined in that layer's style by RhiSceneRenderer, frame after frame, with the renderer features games
// ship with (temporal upscaling, occlusion culling, GPU instance culling) in every combination.
template <class Device>
void CheckSceneOutlineRendering(Device &device, const PlutoGE::render::BasicRendererShaderPackage &shaders,
    const std::function<std::vector<std::byte>(PlutoGE::render::rhi::TextureHandle)> &readPixels)
{
    using namespace PlutoGE::render;
    const auto require = [](bool ok, const std::string &message) { if (!ok) throw std::runtime_error(message); };
    const auto quad = [](float left, float right)
    {
        MeshConfig config;
        for (const auto &corner : std::vector<std::array<float, 2>>{{left, -.3f}, {right, -.3f}, {right, .3f}, {left, -.3f}, {right, .3f}, {left, .3f}})
            config.data.vertices.push_back({{corner[0], corner[1], 0.5f}, {0, 0, 1}, {0, 0}, {1, 0, 0, 1}});
        for (std::uint32_t index = 0; index < config.data.vertices.size(); ++index)
            config.data.indices.push_back(index);
        return std::make_unique<Mesh>(config);
    };
    auto leftMesh = quad(-.7f, -.1f), rightMesh = quad(.1f, .7f);
    Material blue({.emission = {0, 0, 1}});
    constexpr std::uint32_t pickup = 7, objective = 9;
    const std::array commands{RenderCommand{.material = &blue, .mesh = leftMesh.get(), .ownerEntity = pickup},
                              RenderCommand{.material = &blue, .mesh = rightMesh.get(), .ownerEntity = objective}};
    const CameraData camera{.view = glm::mat4(1), .projection = glm::mat4(1)};
    BasicLighting lighting;
    lighting.ambientIntensity = lighting.directionalIntensity = 0;
    const std::array layers{OutlineLayer{{.color = {1, 0, 0, 1}, .width = 2.0f, .hiddenOpacity = 0.0f}, {pickup}},
                            OutlineLayer{{.color = {1, 1, 0, 1}, .width = 2.0f, .hiddenOpacity = 0.0f}, {objective}}};
    for (int configuration = 0; configuration < 8; ++configuration)
    {
        RhiSceneRenderer renderer;
        require(renderer.Initialize(device, shaders), "Scene outline renderer initialization failed");
        if (configuration & 1)
            renderer.SetTemporalUpscalerOptions({.technology = rhi::TemporalUpscaler::Fsr2});
        if (configuration & 2)
        {
            RenderingSettings settings;
            settings.occlusionCulling = OcclusionMode::Cull;
            renderer.SetRenderingSettings(settings);
        }
        if (configuration & 4)
            renderer.SetGpuInstanceCulling(true);
        renderer.SetOutlines(layers);
        for (int frame = 0; frame < 4; ++frame)
        {
            require(renderer.Render(128, 96, camera, lighting, commands, commands), "Scene outline render failed");
            const auto pixels = readPixels(renderer.GetColorTexture());
            std::size_t red = 0, yellow = 0;
            for (std::size_t i = 0; i + 3 < pixels.size(); i += 4)
            {
                const int r = int(pixels[i]), g = int(pixels[i + 1]), b = int(pixels[i + 2]);
                red += r > 150 && g < 60 && b < 60;
                yellow += r > 150 && g > 150 && b < 60;
            }
            require(red >= 20 && yellow >= 20, "Scene outline layers missing (configuration " + std::to_string(configuration) +
                                                   ", frame " + std::to_string(frame) + "): red=" + std::to_string(red) +
                                                   " yellow=" + std::to_string(yellow));
        }
        // An empty set turns outlines off.
        renderer.SetOutlines({});
        require(renderer.Render(128, 96, camera, lighting, commands, commands), "Scene outline render failed");
        const auto pixels = readPixels(renderer.GetColorTexture());
        for (std::size_t i = 0; i + 3 < pixels.size(); i += 4)
            require(!(int(pixels[i]) > 150 && int(pixels[i + 2]) < 60), "Outlines drawn after they were turned off");
    }
}
