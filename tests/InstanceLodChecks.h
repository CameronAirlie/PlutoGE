#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
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
    // The statistics checked here count CPU-binned instances (GpuInstanceCulling has its own check).
    renderer.SetGpuInstanceCulling(false);
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
        models->push_back(glm::translate(glm::mat4(1), glm::vec3((i % 8) * 0.2f - 0.7f, (i / 8) * 0.2f - 0.7f, -1.2f)));
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
    if (chain->Select(30.0f) != 0u || chain->Select(7.0f) != 1u)
        throw std::runtime_error("Instance LOD selection must cap triangles per projected pixel");

    // Near a switch (here LOD0's density limit, about 21 px) an instance bins into both LODs with complementary
    // dither codes, so the change fades instead of popping; away from it, into one LOD drawn whole.
    const float densitySwitch = std::sqrt(200.0f / (BasicInstanceLodChain::MaxTrianglesPerPixel * 3.14159265f));
    const auto across = chain->SelectBlend(densitySwitch);
    if (across.count != 2 || across.lods[0] != 0u || across.lods[1] != 1u ||
        across.codes[1] != (BasicInstanceLodChain::DitherCoarser | across.codes[0]) || across.codes[0] == 0u ||
        std::abs(int(across.codes[0]) - 64) > 2)
        throw std::runtime_error("An instance at a LOD switch must draw both LODs with complementary dithers");
    for (const float pixels : {30.0f, 7.0f})
    {
        const auto whole = chain->SelectBlend(pixels);
        if (whole.count != 1 || whole.codes[0] != 0u || whole.lods[0] != chain->Select(pixels))
            throw std::runtime_error("An instance away from a switch must draw one LOD whole, as Select chooses");
    }
    // A mesh handing small instances to its impostor and the impostor's chain derive the same fade at the hand-over,
    // each drawing only its own side.
    auto meshSide = *chain, impostorSide = *chain;
    meshSide.minScreenRadiusPixels = impostorSide.maxScreenRadiusPixels = 40.0f;
    const auto meshBlend = meshSide.SelectBlend(40.0f), impostorBlend = impostorSide.SelectBlend(40.0f);
    if (meshBlend.count != 1 || impostorBlend.count != 1 || meshBlend.codes[0] == 0u ||
        impostorBlend.codes[0] != (BasicInstanceLodChain::DitherCoarser | meshBlend.codes[0]))
        throw std::runtime_error("A mesh and its impostor must crossfade at their hand-over");
    if (meshSide.SelectBlend(55.0f).count != 1 || impostorSide.SelectBlend(55.0f).count != 0 ||
        meshSide.SelectBlend(28.0f).count != 0 || impostorSide.SelectBlend(28.0f).count != 1)
        throw std::runtime_error("Outside the hand-over band only one of mesh and impostor may draw an instance");

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

    // Foliage detail scales projected sizes before selection: at 0.5 the near instances (about 30 px) select as 15 px
    // ones, whose 200-triangle LOD0 exceeds their density budget, so every instance draws the coarse LOD.
    draw.instanceDrawDistance = std::numeric_limits<float>::max();
    draw.instanceModels = models;
    lighting.foliageDetail = 0.5f;
    const auto coarse = triangles(true);
    lighting.foliageDetail = 1.0f;
    std::cout << "Instance LODs: " << coarse << " triangles at foliage detail 0.5\n";
    if (coarse != 128u * 2u)
        throw std::runtime_error("Reduced foliage detail must select coarser instance LODs");

    // A cluster whose bounds lie wholly beyond its draw distance (or outside the view) is skipped before binning and
    // recording: nothing is drawn, and every instance counts as culled.
    draw.shadowBoundsCenter = {0.0f, 0.0f, -400.0f};
    draw.shadowBoundsRadius = 20.0f;
    draw.instanceDrawDistance = 100.0f;
    const auto far = std::make_shared<std::vector<glm::mat4>>(models->begin() + 64, models->end());
    draw.instanceModels = far;
    const auto clusterTriangles = triangles(true);
    const auto &clusterStats = renderer.GetFrameStats();
    std::cout << "Instance LODs: " << clusterTriangles << " triangles and " << clusterStats.geometryDraws
              << " draws for a cluster beyond its draw distance\n";
    if (clusterTriangles != 0u || clusterStats.geometryDraws != 0u || clusterStats.viewCulledInstances != 64u)
        throw std::runtime_error("A cluster beyond its draw distance must be skipped whole");
    draw.shadowBoundsRadius = -1.0f;
}
