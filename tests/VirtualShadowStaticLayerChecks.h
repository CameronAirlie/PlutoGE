#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

// A dynamic (unrevised) caster moving over static (revised) ones must redraw only itself: its pages restore the static
// layer instead of drawing the static casters again, and the result matches rendering every caster as static.
template<class ReadPixels>
void CheckVirtualShadowStaticLayer(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    const auto previousWidth = renderer.GetWidth(), previousHeight = renderer.GetHeight();
    renderer.Resize(64, 64);
    std::vector<BasicVertex> vertices;
    std::vector<std::uint32_t> indices;
    constexpr int divisions = 40;
    for (int y = 0; y <= divisions; ++y)
        for (int x = 0; x <= divisions; ++x)
        {
            BasicVertex vertex{};
            vertex.position = {float(x) / divisions - 0.5f, float(y) / divisions - 0.5f, 0};
            vertex.normal = {0, 0, 1}; vertex.uv = {float(x) / divisions, float(y) / divisions};
            vertices.push_back(vertex);
        }
    for (int y = 0; y < divisions; ++y)
        for (int x = 0; x < divisions; ++x)
        {
            const auto a = std::uint32_t(y * (divisions + 1) + x), b = a + divisions + 1;
            indices.insert(indices.end(), {a, a + 1, b + 1, a, b + 1, b});
        }
    constexpr std::uint32_t staticTriangles = divisions * divisions * 2;
    auto grid = renderer.CreateMesh({vertices, indices});
    std::vector<BasicVertex> quadVertices(4);
    for (int corner = 0; corner < 4; ++corner)
    {
        quadVertices[corner].position = {corner % 2 ? 0.5f : -0.5f, corner / 2 ? 0.5f : -0.5f, 0};
        quadVertices[corner].normal = {0, 0, 1};
    }
    const std::vector<std::uint32_t> quadIndices{0, 1, 3, 0, 3, 2};
    auto quad = renderer.CreateMesh({quadVertices, quadIndices});

    BasicDraw receiver;
    receiver.mesh = &grid;
    receiver.model = glm::translate(glm::mat4(1), glm::vec3(0, 0, .8f)) * glm::scale(glm::mat4(1), glm::vec3(18, 18, 1));
    receiver.castsShadow = false;
    receiver.preparationRevision = 1;
    // Static: a perforated layer of grid casters (revised packets). Dynamic: one small unrevised quad above them.
    std::vector<BasicDraw> casters(5);
    for (std::size_t index = 0; index < 4; ++index)
    {
        auto &draw = casters[index];
        draw.mesh = &grid;
        draw.model = glm::translate(glm::mat4(1), glm::vec3(float(index % 2) * 3.0f - 1.5f, float(index / 2) * 3.0f - 1.5f, .4f)) *
                     glm::scale(glm::mat4(1), glm::vec3(2.0f, 2.0f, 1));
        draw.preparationRevision = 2 + index;
    }
    auto &dynamic = casters.back();
    dynamic.mesh = &quad;
    dynamic.model = glm::translate(glm::mat4(1), glm::vec3(-1.5f, -1.5f, .2f)) * glm::scale(glm::mat4(1), glm::vec3(.6f, .6f, 1));
    BasicLighting lighting;
    lighting.shadowsEnabled = true; lighting.shadowMethod = ShadowMethod::Virtual;
    lighting.shadowDistance = 40; lighting.directionalDirection = {0, 0, 1};
    lighting.shadowSoftness = 1;
    lighting.virtualShadowCoarseMinCasterTexels = 0;
    const auto projection = glm::scale(glm::mat4(1), glm::vec3(.1f, .1f, 1));
    const std::vector<BasicDraw> receivers{receiver};
    const auto render = [&]
    {
        for (auto &matrix : lighting.shadowMatrices) matrix = projection * lighting.view;
        renderer.Render(projection * lighting.view, lighting, receivers, {}, casters, PostProcessDebugView::DirectionalShadowMaskFiltered);
    };
    for (int frame = 0; frame < 120; ++frame) render();
    if (!renderer.GetFrameStats().virtualShadows.staticLayer || renderer.GetFrameStats().virtualShadows.dynamicCasterChunks == 0)
        throw std::runtime_error("A frame with static and dynamic casters did not use the VSM static layer");

    // Moving only the dynamic caster: no update redraws the static casters, so no frame submits their triangles.
    std::uint64_t moving = 0;
    for (int frame = 0; frame < 24; ++frame)
    {
        dynamic.model[3].x = -1.5f + float(frame) * 0.05f;
        render();
        if (frame < 8) continue;
        const auto &stats = renderer.GetFrameStats().virtualShadows;
        if (stats.staticDirty != 0 || stats.submittedTriangles >= staticTriangles)
            throw std::runtime_error("A moving dynamic VSM caster redrew static casters: " + std::to_string(stats.staticDirty) +
                                     " static-dirty pages, " + std::to_string(stats.submittedTriangles) + " triangles");
        moving += stats.updated;
    }
    if (moving == 0) throw std::runtime_error("The moving dynamic VSM caster updated no pages");
    for (int frame = 0; frame < 40; ++frame) render();
    const auto layered = readPixels(renderer.GetColorTexture());

    // The same caster made static redraws its pages' static casters, and the image must not change.
    dynamic.preparationRevision = 100;
    for (int frame = 0; frame < 40; ++frame) render();
    if (renderer.GetFrameStats().virtualShadows.dynamicCasterChunks != 0)
        throw std::runtime_error("A revised caster stayed dynamic");
    const auto reference = readPixels(renderer.GetColorTexture());
    if (layered.size() != reference.size() || std::memcmp(layered.data(), reference.data(), layered.size()) != 0)
        throw std::runtime_error("VSM static layer restore differs from drawing every caster");
    std::cout << "VSM static layer: dynamic motion updated " << moving << " pages without static casters\n";
    renderer.Resize(previousWidth, previousHeight);
}
