#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

// GPU instance culling (GpuInstanceCulling, InstanceCulling.slang) bins instances as the CPU does: the same frame
// rendered with GPU and CPU binning is identical, for instances near (LOD0) and far (LOD1), outside the view, beyond
// the draw distance and below the chain's covered size.
template <class ReadPixels>
inline void CheckGpuInstanceCulling(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    if (!renderer.GpuInstanceCullingAvailable())
    {
        std::cout << "GPU instance culling: unavailable on this device; skipped\n";
        return;
    }
    renderer.Resize(64, 64);
    // LOD0: a 4 x 4 grid of quads; LOD1: one quad, after it in the index buffer.
    std::vector<BasicVertex> vertices;
    for (int y = 0; y <= 4; ++y)
        for (int x = 0; x <= 4; ++x)
            vertices.push_back({{{x * 0.25f - 0.5f, y * 0.25f - 0.5f, 0.0f}}, {{0, 0, 1}}, {{x * 0.25f, y * 0.25f}}});
    std::vector<std::uint32_t> indices;
    for (std::uint32_t y = 0; y < 4; ++y)
        for (std::uint32_t x = 0; x < 4; ++x)
        {
            const std::uint32_t a = y * 5 + x;
            indices.insert(indices.end(), {a, a + 1, a + 6, a, a + 6, a + 5});
        }
    const auto lod0Count = static_cast<std::uint32_t>(indices.size());
    indices.insert(indices.end(), {0, 4, 24, 0, 24, 20});
    auto mesh = renderer.CreateMesh({vertices, indices});

    auto models = std::make_shared<std::vector<glm::mat4>>();
    const auto place = [&](glm::vec3 position) { models->push_back(glm::translate(glm::mat4(1), position)); };
    for (int i = 0; i < 6; ++i) place({i * 1.1f - 2.75f, -1.0f, -4.0f});    // near: LOD0
    for (int i = 0; i < 6; ++i) place({i * 4.0f - 10.0f, 4.0f, -30.0f});   // far: LOD1
    for (int i = 0; i < 4; ++i) place({i * 2.0f - 3.0f, 0.0f, 6.0f});      // behind the camera
    for (int i = 0; i < 4; ++i) place({i * 6.0f - 9.0f, 0.0f, -90.0f});    // beyond the draw distance
    for (int i = 0; i < 4; ++i) place({i * 30.0f - 45.0f, -10.0f, -60.0f}); // below the covered size
    auto chain = std::make_shared<BasicInstanceLodChain>();
    chain->boundsRadius = 0.71f;
    chain->lods = {{0, lod0Count, 1e30f}, {lod0Count, 6, 4.0f}};
    chain->minScreenRadiusPixels = 1.0f;

    BasicLighting lighting;
    lighting.shadowsEnabled = false;
    lighting.cameraPosition = {0, 0, 0};
    lighting.view = glm::mat4(1);
    const auto projection = glm::perspectiveRH_ZO(glm::radians(60.f), 1.f, 1000.f, .1f);
    BasicDraw draw;
    draw.mesh = &mesh;
    draw.emission = {1, 1, 1};
    draw.castsShadow = false;
    draw.firstIndex = 0;
    draw.indexCount = lod0Count;
    draw.instanceModels = models;
    draw.instanceLods = chain;
    draw.instanceDrawDistance = 70.0f;
    const auto render = [&](bool gpu)
    {
        renderer.SetGpuInstanceCulling(gpu);
        // Twice: the second frame also culls against the first's depth when occlusion is on.
        renderer.Render(projection * lighting.view, lighting, std::span(&draw, 1));
        renderer.Render(projection * lighting.view, lighting, std::span(&draw, 1));
        return std::pair(readPixels(renderer.GetColorTexture()), renderer.GetFrameStats().gpuBinnedDraws);
    };
    const auto [cpu, cpuBinned] = render(false);
    const auto [gpu, gpuBinned] = render(true);
    std::size_t different = 0, drawn = 0;
    for (std::size_t texel = 0; texel + 3 < cpu.size(); texel += 4)
    {
        different += cpu[texel] != gpu[texel] || cpu[texel + 1] != gpu[texel + 1] || cpu[texel + 2] != gpu[texel + 2];
        drawn += static_cast<unsigned>(cpu[texel]) > 128u;
    }
    std::cout << "GPU instance culling: " << gpuBinned << " draws binned on the GPU, " << different << " of "
              << cpu.size() / 4 << " pixels differ from CPU binning (" << drawn << " lit)\n";
    renderer.SetGpuInstanceCulling(false);
    if (cpuBinned != 0 || gpuBinned != 1)
        throw std::runtime_error("GPU instance culling must bin the instanced draw only when enabled");
    if (drawn == 0)
        throw std::runtime_error("The instance culling scene drew nothing");
    if (different != 0)
        throw std::runtime_error("GPU instance culling must draw what CPU binning draws");
}
