#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

// Per-chunk LOD of instanced draws (BasicDraw::instanceLods): one draw of 128 instances, the first 64 beside the
// camera and the rest far away, must draw its near chunk at LOD0 and its far chunk at the coarse LOD, even though
// the draw as a whole carries the full-detail range.
inline void CheckInstanceLods(PlutoGE::render::BasicRenderer &renderer)
{
    using namespace PlutoGE::render;
    renderer.Resize(64, 64);
    // LOD0: a 10 x 10 grid of quads (200 triangles); LOD1: one quad (2 triangles), after it in the index buffer.
    std::vector<BasicVertex> vertices;
    for (int y = 0; y <= 10; ++y)
        for (int x = 0; x <= 10; ++x)
            vertices.push_back({{{x * 0.1f - 0.5f, y * 0.1f - 0.5f, 0.0f}}, {{0, 0, 1}}, {{x * 0.1f, y * 0.1f}}});
    std::vector<std::uint32_t> indices;
    for (std::uint32_t y = 0; y < 10; ++y)
        for (std::uint32_t x = 0; x < 10; ++x)
        {
            const std::uint32_t a = y * 11 + x;
            indices.insert(indices.end(), {a, a + 1, a + 12, a, a + 12, a + 11});
        }
    const auto lod0Count = static_cast<std::uint32_t>(indices.size());
    indices.insert(indices.end(), {0, 10, 120, 0, 120, 110});
    auto mesh = renderer.CreateMesh({vertices, indices});

    auto models = std::make_shared<std::vector<glm::mat4>>();
    for (int i = 0; i < 64; ++i)
        models->push_back(glm::translate(glm::mat4(1), glm::vec3((i % 8) * 0.2f - 0.7f, (i / 8) * 0.2f - 0.7f, -3.0f)));
    for (int i = 0; i < 64; ++i)
        models->push_back(glm::translate(glm::mat4(1), glm::vec3((i % 8) * 4.0f - 14.0f, (i / 8) * 4.0f - 14.0f, -400.0f)));
    auto chain = std::make_shared<BasicInstanceLodChain>();
    chain->boundsRadius = 0.71f;
    chain->lods = {{0, lod0Count, 1e30f}, {lod0Count, 6, 6.0f}};

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
    const auto triangles = [&](bool perChunk)
    {
        draw.instanceLods = perChunk ? chain : nullptr;
        renderer.Render(projection * lighting.view, lighting, std::span(&draw, 1));
        const auto &stats = renderer.GetFrameStats();
        return stats.geometryTriangles[0] + stats.geometryTriangles[1];
    };
    const auto whole = triangles(false), chunked = triangles(true);
    std::cout << "Instance LODs: " << whole << " triangles per cluster LOD, " << chunked << " per chunk\n";
    if (whole != 128u * 200u)
        throw std::runtime_error("Without a chain every instance must draw the draw's own range");
    if (chunked != 64u * 200u + 64u * 2u)
        throw std::runtime_error("The far instance chunk must draw its coarse LOD and the near chunk LOD0");

    // Beyond the screen-radius thresholds, a LOD too dense for its projected size gives way to the next: at 7 px the
    // thresholds keep LOD0, but its 200 triangles exceed the budget of a 7 px disc.
    if (chain->Select(20.0f) != 0u || chain->Select(7.0f) != 1u)
        throw std::runtime_error("Instance LOD selection must cap triangles per projected pixel");

    // Binned instances outside the view are skipped: add some behind the camera and far to each side.
    auto withHidden = std::make_shared<std::vector<glm::mat4>>(*models);
    for (int i = 0; i < 8; ++i)
    {
        withHidden->push_back(glm::translate(glm::mat4(1), glm::vec3(i * 2.0f - 7.0f, 0.0f, 5.0f)));
        withHidden->push_back(glm::translate(glm::mat4(1), glm::vec3(i % 2 ? 40.0f : -40.0f, 0.0f, -10.0f - i)));
    }
    draw.instanceModels = withHidden;
    const auto culled = triangles(true);
    const auto skipped = renderer.GetFrameStats().viewCulledInstances;
    std::cout << "Instance LODs: " << culled << " triangles with " << skipped << " instances outside the view\n";
    if (culled != chunked || skipped != 16u)
        throw std::runtime_error("Instances outside the view must be skipped by per-instance LOD binning");

    // So are instances beyond the draw distance: at 100 m the far chunk (400 m away) goes.
    draw.instanceDrawDistance = 100.0f;
    const auto near = triangles(true);
    const auto beyond = renderer.GetFrameStats().viewCulledInstances;
    std::cout << "Instance LODs: " << near << " triangles within 100 m, " << beyond << " instances skipped\n";
    if (near != 64u * 200u || beyond != 16u + 64u)
        throw std::runtime_error("Instances beyond the draw distance must be skipped by per-instance LOD binning");
}
