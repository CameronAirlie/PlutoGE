#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

// GPU instance batching (GpuInstanceCulling::Batch, GpuInstanceBatchCursor): GPU-binned draws that bind the same state
// share multi-draw batches, and each pass records a batch once. The image must not change: a frame of foliage-like
// cells drawn with batching matches the same frame without it and with CPU binning, in the depth prepass, both culling
// phases (occlusion after a wall disappears), the shading pass and the outline pass. Cells share a batch even with a
// draw of another surface recorded between them, which gets a batch of its own; `expectBatching` (devices with
// indirect first instances) requires that batches form at all.
template <class ReadPixels>
inline void CheckGpuInstanceBatching(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels, bool expectBatching)
{
    using namespace PlutoGE::render;
    if (!renderer.GpuInstanceCullingAvailable())
    {
        std::cout << "GPU instance batching: GPU instance culling unavailable on this device; skipped\n";
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
    auto chain = std::make_shared<BasicInstanceLodChain>();
    chain->boundsRadius = 0.71f;
    chain->lods = {{0, lod0Count, 1e30f}, {lod0Count, 6, 4.0f}};
    chain->minScreenRadiusPixels = 1.0f;

    // Six cells in a row of columns, near (LOD0) and far (LOD1) instances each; the fourth has another surface. Batches
    // draw their members where their first member is recorded, so no two cells' quads may overlap at equal depth (a
    // tie only draw order decides).
    std::vector<BasicDraw> cells;
    for (int cell = 0; cell < 6; ++cell)
    {
        auto models = std::make_shared<std::vector<glm::mat4>>();
        const float x = cell * 0.9f - 2.25f;
        const auto place = [&](glm::vec3 position)
        { models->push_back(glm::scale(glm::translate(glm::mat4(1), position), glm::vec3(0.8f))); };
        place({x, -1.0f, -4.0f});
        place({x, 0.2f, -4.5f});
        place({x * 4.0f, 4.0f, -30.0f});
        place({x * 4.0f, -6.0f, -30.0f});
        BasicDraw draw;
        draw.mesh = &mesh;
        draw.emission = cell == 3 ? glm::vec3(0.2f, 1.0f, 0.2f) : glm::vec3(1.0f);
        draw.castsShadow = false;
        draw.firstIndex = 0;
        draw.indexCount = lod0Count;
        draw.instanceModels = models;
        draw.instanceLods = chain;
        draw.instanceDrawDistance = 70.0f;
        cells.push_back(std::move(draw));
    }

    BasicLighting lighting;
    lighting.shadowsEnabled = false;
    lighting.cameraPosition = {0, 0, 0};
    lighting.view = glm::mat4(1);
    const auto projection = glm::perspectiveRH_ZO(glm::radians(60.f), 1.f, 1000.f, .1f);
    struct Frame
    {
        decltype(readPixels(renderer.GetColorTexture())) pixels;
        std::size_t draws = 0, batches = 0;
        bool secondPhase = false;
    };
    // Renders `frames` (the last is read back) with CPU binning, or GPU binning with or without batching.
    enum class Mode { Cpu, Gpu, GpuBatched };
    const auto render = [&](Mode mode, std::span<const std::vector<BasicDraw>> frames)
    {
        renderer.SetGpuInstanceCulling(mode != Mode::Cpu);
        renderer.SetGpuInstanceBatching(mode == Mode::GpuBatched);
        for (const auto &frame : frames)
            renderer.Render(projection * lighting.view, lighting, std::span(frame));
        const auto &stats = renderer.GetFrameStats();
        Frame result{readPixels(renderer.GetColorTexture()), stats.gpuBinnedDraws, stats.gpuBinnedBatches,
                     stats.gpuCullingSecondPhase};
        renderer.SetGpuInstanceCulling(false);
        renderer.SetGpuInstanceBatching(true);
        return result;
    };
    const auto compare = [](const std::string &label, const Frame &expected, const Frame &actual)
    {
        std::size_t different = 0, lit = 0;
        for (std::size_t texel = 0; texel + 3 < expected.pixels.size(); texel += 4)
        {
            different += expected.pixels[texel] != actual.pixels[texel] || expected.pixels[texel + 1] != actual.pixels[texel + 1] ||
                         expected.pixels[texel + 2] != actual.pixels[texel + 2];
            lit += static_cast<unsigned>(expected.pixels[texel + 1]) > 128u;
        }
        std::cout << "GPU instance batching (" << label << "): " << different << " of " << expected.pixels.size() / 4
                  << " pixels differ (" << lit << " lit)\n";
        if (lit == 0)
            throw std::runtime_error("The instance batching scene drew nothing (" + label + ")");
        if (different != 0)
            throw std::runtime_error("GPU instance batching must not change the image (" + label + ")");
    };
    const auto requireBatches = [&](const std::string &label, const Frame &unbatched, const Frame &batched,
                                     std::size_t expectedBatches)
    {
        std::cout << "GPU instance batching (" << label << "): " << batched.draws << " binned draws in " << batched.batches
                  << " batches (" << unbatched.batches << " without batching)\n";
        if (unbatched.batches != unbatched.draws)
            throw std::runtime_error("Without batching every binned draw must be a batch of its own");
        if (batched.draws != unbatched.draws)
            throw std::runtime_error("Batching must not change which draws are binned");
        if (batched.batches != (expectBatching ? expectedBatches : batched.draws))
            throw std::runtime_error("Cells binding the same state must share a batch, other surfaces their own (" + label + ")");
    };

    // Two frames each: the second also culls against the first's depth when occlusion is on.
    const std::vector<std::vector<BasicDraw>> twice{cells, cells};
    {
        const auto cpu = render(Mode::Cpu, twice), gpu = render(Mode::Gpu, twice), batched = render(Mode::GpuBatched, twice);
        compare("against CPU binning", cpu, batched);
        compare("against unbatched GPU binning", gpu, batched);
        requireBatches("cells", gpu, batched, 2);
    }

    // Outline shells draw in a pass of their own after the shading pass: their batches must be drawn again there.
    // Shells extrude along vertex normals and cull front faces, so they show around closed meshes: boxes here (faces
    // with their own vertices), with both LODs drawing the whole box.
    std::vector<BasicVertex> boxVertices;
    std::vector<std::uint32_t> boxIndices;
    for (int axis = 0; axis < 3; ++axis)
        for (const float side : {-1.0f, 1.0f})
        {
            glm::vec3 normal(0.0f), u(0.0f), v(0.0f);
            normal[axis] = side;
            u[(axis + 1) % 3] = 1.0f;
            v[(axis + 2) % 3] = 1.0f;
            if (side < 0.0f) std::swap(u, v);
            const auto first = static_cast<std::uint32_t>(boxVertices.size());
            for (const auto corner : {glm::vec2(-1, -1), glm::vec2(1, -1), glm::vec2(1, 1), glm::vec2(-1, 1)})
            {
                const glm::vec3 position = 0.3f * (normal + corner.x * u + corner.y * v);
                boxVertices.push_back({{{position.x, position.y, position.z}}, {{normal.x, normal.y, normal.z}}, {{0, 0}}});
            }
            boxIndices.insert(boxIndices.end(), {first, first + 1, first + 2, first, first + 2, first + 3});
        }
    auto box = renderer.CreateMesh({boxVertices, boxIndices});
    auto boxChain = std::make_shared<BasicInstanceLodChain>(*chain);
    boxChain->boundsRadius = 0.52f;
    boxChain->lods = {{0, static_cast<std::uint32_t>(boxIndices.size()), 1e30f},
                      {0, static_cast<std::uint32_t>(boxIndices.size()), 4.0f}};
    {
        auto outlined = cells;
        for (auto &cell : outlined)
        {
            cell.mesh = &box;
            cell.indexCount = static_cast<std::uint32_t>(boxIndices.size());
            cell.instanceLods = boxChain;
            cell.outlineWidth = 0.05f;
            cell.outlineColor = {1.0f, 0.0f, 1.0f};
        }
        const std::vector<std::vector<BasicDraw>> frames{outlined, outlined};
        const auto gpu = render(Mode::Gpu, frames), batched = render(Mode::GpuBatched, frames);
        std::size_t outlinePixels = 0;
        for (std::size_t texel = 0; texel + 3 < batched.pixels.size(); texel += 4)
            outlinePixels += static_cast<unsigned>(batched.pixels[texel]) > 128u && static_cast<unsigned>(batched.pixels[texel + 1]) < 64u;
        std::cout << "GPU instance batching (outlined): " << outlinePixels << " outline pixels\n";
        if (outlinePixels == 0)
            throw std::runtime_error("The outlined batching scene must show outline shells");
        compare("outlined", gpu, batched);
        requireBatches("outlined", gpu, batched, 2);
    }

    // Two culling phases: a wall hides every cell for two frames and is gone in the third. That frame's first phase
    // still hides the cells; the second phase draws its batches' slots of its own.
    {
        BasicDraw wall;
        wall.mesh = &mesh;
        wall.castsShadow = false;
        wall.firstIndex = 0;
        wall.indexCount = lod0Count;
        wall.model = glm::scale(glm::translate(glm::mat4(1), {0.0f, 0.0f, -2.0f}), glm::vec3(20.0f));
        auto walled = cells;
        walled.insert(walled.begin(), wall);
        const std::vector<std::vector<BasicDraw>> frames{walled, walled, cells};
        lighting.occlusionMode = OcclusionMode::Cull;
        const auto cpu = render(Mode::Cpu, frames), gpu = render(Mode::Gpu, frames), batched = render(Mode::GpuBatched, frames);
        lighting.occlusionMode = OcclusionMode::Off;
        if (!batched.secondPhase)
            throw std::runtime_error("GPU instance batching must be checked with a second culling phase");
        compare("uncovered, two culling phases, against CPU binning", cpu, batched);
        compare("uncovered, two culling phases", gpu, batched);
        requireBatches("two culling phases", gpu, batched, 2);
    }
}
