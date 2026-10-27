#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/OcclusionCulling.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// GPU-driven opaque draws (bindless materials, multi-draw indirect) must produce the image of the bound-material
// path: textured and untextured materials, mirrored and two-sided surfaces, GPU occlusion culling, motion history
// and the geometry layouts that post effects select.
template <class ReadPixels>
void CheckGpuDrivenOpaque(PlutoGE::render::BasicRenderer &renderer, PlutoGE::render::rhi::IRenderDevice &device,
                          ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    const auto require = [](bool value, const std::string &message) { if (!value) throw std::runtime_error(message); };
    constexpr std::uint32_t width = 160, height = 120;
    require(renderer.Resize(width, height), "GPU-driven fixture resize failed");

    constexpr std::array<BasicVertex, 8> cubeVertices = {{
        {{{-0.5f, -0.5f, -0.5f}}, {{-0.577f, -0.577f, -0.577f}}, {{0, 0}}},
        {{{0.5f, -0.5f, -0.5f}}, {{0.577f, -0.577f, -0.577f}}, {{1, 0}}},
        {{{0.5f, 0.5f, -0.5f}}, {{0.577f, 0.577f, -0.577f}}, {{1, 1}}},
        {{{-0.5f, 0.5f, -0.5f}}, {{-0.577f, 0.577f, -0.577f}}, {{0, 1}}},
        {{{-0.5f, -0.5f, 0.5f}}, {{-0.577f, -0.577f, 0.577f}}, {{0, 0}}},
        {{{0.5f, -0.5f, 0.5f}}, {{0.577f, -0.577f, 0.577f}}, {{1, 0}}},
        {{{0.5f, 0.5f, 0.5f}}, {{0.577f, 0.577f, 0.577f}}, {{1, 1}}},
        {{{-0.5f, 0.5f, 0.5f}}, {{-0.577f, 0.577f, 0.577f}}, {{0, 1}}},
    }};
    constexpr std::array<std::uint32_t, 36> cubeIndices = {
        0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 4, 7, 0, 7, 3,
        1, 2, 6, 1, 6, 5, 3, 7, 6, 3, 6, 2, 0, 1, 5, 0, 5, 4};
    constexpr std::array<BasicVertex, 4> quadVertices = {{
        {{{-1, 0, -1}}, {{0, 1, 0}}, {{0, 0}}}, {{{1, 0, -1}}, {{0, 1, 0}}, {{4, 0}}},
        {{{1, 0, 1}}, {{0, 1, 0}}, {{4, 4}}}, {{{-1, 0, 1}}, {{0, 1, 0}}, {{0, 4}}}}};
    constexpr std::array<std::uint32_t, 6> quadIndices = {0, 2, 1, 0, 3, 2};
    auto cube = renderer.CreateMesh({cubeVertices, cubeIndices});
    auto quad = renderer.CreateMesh({quadVertices, quadIndices});

    // Two small material textures: a checker base colour and a roughness/metal gradient.
    std::vector<std::byte> checker(8 * 8 * 4), gradient(8 * 8 * 4);
    for (std::uint32_t texel = 0; texel < 64; ++texel)
    {
        const bool odd = ((texel % 8) + texel / 8) % 2 != 0;
        for (std::uint32_t channel = 0; channel < 3; ++channel)
            checker[texel * 4 + channel] = std::byte(odd ? 230 : 40 + 60 * channel);
        checker[texel * 4 + 3] = std::byte{255};
        gradient[texel * 4] = gradient[texel * 4 + 1] = gradient[texel * 4 + 2] = std::byte(texel * 4);
        gradient[texel * 4 + 3] = std::byte{255};
    }
    rhi::Texture checkerTexture(device, device.CreateTexture(
        {8, 8, rhi::Format::R8G8B8A8Srgb, rhi::TextureUsage::Sampled, "GPU-driven checker"}, checker));
    rhi::Texture gradientTexture(device, device.CreateTexture(
        {8, 8, rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::Sampled, "GPU-driven gradient"}, gradient));

    std::vector<BasicDraw> draws;
    BasicDraw ground;
    ground.mesh = &quad;
    ground.model = glm::scale(glm::translate(glm::mat4(1), glm::vec3(0, -0.6f, -4)), glm::vec3(12, 1, 12));
    ground.baseColorTexture = checkerTexture.Get();
    ground.roughnessTexture = gradientTexture.Get();
    ground.uvScale = {2, 2};
    draws.push_back(ground);
    for (int index = 0; index < 48; ++index)
    {
        BasicDraw draw;
        draw.mesh = &cube;
        const float x = float(index % 8) * 1.4f - 4.9f, z = -float(index / 8) * 1.6f;
        auto model = glm::translate(glm::mat4(1), glm::vec3(x, 0, z)) * glm::rotate(glm::mat4(1), 0.3f * index, glm::vec3(0, 1, 0));
        if (index % 5 == 0)
            model = glm::scale(model, glm::vec3(-1, 1, 1)); // mirrored: front-face culling
        draw.model = model;
        draw.previousModel = glm::translate(model, glm::vec3(0.02f * (index % 3), 0, 0));
        draw.baseColor = {0.3f + 0.1f * (index % 7), 0.5f, 0.9f - 0.1f * (index % 4), 1};
        draw.metallic = (index % 3) * 0.4f;
        draw.roughness = 0.3f + (index % 4) * 0.2f;
        draw.twoSided = index % 7 == 0;
        if (index % 2 == 0) draw.baseColorTexture = checkerTexture.Get();
        if (index % 3 == 0) draw.metallicTexture = gradientTexture.Get();
        if (index % 11 == 0) draw.emission = {0.4f, 0.1f, 0.0f};
        draw.shadowBoundsCenter = glm::vec3(model[3]);
        draw.shadowBoundsRadius = 1.0f;
        OcclusionCulling::SetRigidBounds(draw, glm::vec3(-0.5f), glm::vec3(0.5f));
        draws.push_back(draw);
    }
    // A wall hiding the back rows, so GPU occlusion culls some GPU-driven draws.
    BasicDraw wall;
    wall.mesh = &cube;
    wall.model = glm::scale(glm::translate(glm::mat4(1), glm::vec3(0, 0.5f, -4.4f)), glm::vec3(14, 3, 0.2f));
    wall.shadowBoundsCenter = glm::vec3(wall.model[3]);
    wall.shadowBoundsRadius = 8.0f;
    OcclusionCulling::SetRigidBounds(wall, glm::vec3(-0.5f), glm::vec3(0.5f));
    draws.push_back(wall);

    BasicLighting lighting;
    lighting.view = glm::lookAtRH(glm::vec3(0, 2.5f, 4.5f), glm::vec3(0, 0, -3), glm::vec3(0, 1, 0));
    lighting.cameraPosition = glm::vec3(glm::inverse(lighting.view)[3]);
    lighting.directionalDirection = glm::normalize(glm::vec3(0.3f, -1, -0.4f));
    lighting.ambientIntensity = 0.3f;
    lighting.pointLights.push_back({{1, 1, -1}, 6.0f, {1, 0.6f, 0.3f}, 4.0f});
    glm::mat4 projection(0.0f);
    constexpr float nearPlane = 0.1f, farPlane = 100.0f;
    const float focal = 1.0f / std::tan(glm::radians(60.0f) * 0.5f);
    projection[0][0] = focal / (float(width) / float(height));
    projection[1][1] = focal;
    projection[2][2] = nearPlane / (farPlane - nearPlane);
    projection[2][3] = -1.0f;
    projection[3][2] = farPlane * nearPlane / (farPlane - nearPlane);

    const auto compare = [&](std::span<const BasicPostProcessEffect> effects, OcclusionMode occlusion, const char *label)
    {
        lighting.occlusionMode = occlusion;
        const auto render = [&](bool gpuDriven)
        {
            renderer.SetGpuDrivenOpaqueEnabled(gpuDriven);
            // Two frames: the second has motion history and last frame's occlusion results.
            for (int frame = 0; frame < 2; ++frame)
                renderer.Render(projection * lighting.view, lighting, draws, effects);
            return readPixels(renderer.GetColorTexture());
        };
        const auto expected = render(false);
        require(renderer.GetFrameStats().gpuDrivenDraws == 0, "The bound-material reference used GPU-driven draws");
        const auto actual = render(true);
        const auto &stats = renderer.GetFrameStats();
        require(stats.gpuDrivenDraws == draws.size(), std::string(label) + ": " + std::to_string(stats.gpuDrivenDraws) +
                " of " + std::to_string(draws.size()) + " opaque draws were GPU-driven");
        // Three pipelines (back, front and no culling) over two geometry buffers at most.
        require(stats.gpuDrivenBatches <= 6, std::string(label) + ": GPU-driven draws were not bucketed (" +
                std::to_string(stats.gpuDrivenBatches) + " multi-draw calls)");
        require(actual.size() == expected.size() && !actual.empty(), "GPU-driven readback size mismatch");
        for (std::size_t index = 0; index < actual.size(); ++index)
            if (actual[index] != expected[index])
                throw std::runtime_error(std::string(label) + ": GPU-driven opaque changed the image at byte " +
                                         std::to_string(index));
        std::cout << label << ": " << stats.gpuDrivenDraws << " GPU-driven draws in " << stats.gpuDrivenBatches
                  << " multi-draw calls with " << stats.gpuDrivenMaterials << " bindless materials match the bound path"
                  << (stats.occlusionActive ? " (occlusion culled " + std::to_string(stats.occlusion.rejected) + ")" : "") << "\n";
    };
    compare({}, OcclusionMode::Off, "GPU-driven colour");
    compare({}, OcclusionMode::Cull, "GPU-driven colour, occlusion");
    const std::array surfaceEffects{BasicPostProcessEffect{BasicPostProcessEffectType::SSAO},
                                    BasicPostProcessEffect{BasicPostProcessEffectType::ToneMapping}};
    compare(surfaceEffects, OcclusionMode::Cull, "GPU-driven surface outputs");
    renderer.SetGpuDrivenOpaqueEnabled(true);
}
