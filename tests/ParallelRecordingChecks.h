#pragma once
#include "PlutoGE/core/TaskPool.h"
#include "PlutoGE/render/BasicRenderer.h"
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>

// Parallel recording (shadows and geometry on worker threads) and object streaming (single objects indexed by first
// instance) must leave the image unchanged. Renders one scene with both on and with both off and compares pixels
// exactly: many objects and materials, a mirrored object (culling of streamed draws), a multi-instance draw (shares the
// instanced binding with the stream), an outline, alpha testing, a transparent pane, VSM and point shadows.
template<class ReadPixels>
void CheckParallelRecording(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    const auto require = [](bool condition, const std::string &message) {
        if (!condition) throw std::runtime_error(message);
    };
    const auto width = renderer.GetWidth(), height = renderer.GetHeight();
    renderer.Resize(128, 96);
    constexpr std::array<BasicVertex, 8> vertices = {{
        {{{-.5f,-.5f,-.5f}},{{0,0,-1}},{{0,0}}}, {{{.5f,-.5f,-.5f}},{{0,0,-1}},{{1,0}}},
        {{{.5f,.5f,-.5f}},{{0,0,-1}},{{1,1}}}, {{{-.5f,.5f,-.5f}},{{0,0,-1}},{{0,1}}},
        {{{-.5f,-.5f,.5f}},{{0,0,1}},{{0,0}}}, {{{.5f,-.5f,.5f}},{{0,0,1}},{{1,0}}},
        {{{.5f,.5f,.5f}},{{0,0,1}},{{1,1}}}, {{{-.5f,.5f,.5f}},{{0,0,1}},{{0,1}}}}};
    constexpr std::array<std::uint32_t, 36> indices{
        0,2,1, 0,3,2, 4,5,6, 4,6,7, 0,1,5, 0,5,4, 3,6,2, 3,7,6, 0,4,7, 0,7,3, 1,2,6, 1,6,5};
    auto cube = renderer.CreateMesh({vertices, indices});
    constexpr std::array<BasicVertex, 4> planeVertices = {{
        {{{-1,0,-1}},{{0,1,0}},{{0,0}}}, {{{1,0,-1}},{{0,1,0}},{{1,0}}},
        {{{1,0,1}},{{0,1,0}},{{1,1}}}, {{{-1,0,1}},{{0,1,0}},{{0,1}}}}};
    constexpr std::array<std::uint32_t, 6> planeIndices{0,2,1, 0,3,2};
    auto plane = renderer.CreateMesh({planeVertices, planeIndices});

    std::vector<BasicDraw> draws;
    BasicDraw ground;
    ground.mesh = &plane;
    ground.model = glm::scale(glm::translate(glm::mat4(1), glm::vec3(0, -1, 0)), glm::vec3(8));
    ground.baseColor = {.6f, .6f, .55f, 1};
    draws.push_back(ground);
    for (int i = 0; i < 160; ++i)
    {
        BasicDraw draw;
        draw.mesh = &cube;
        const float x = float(i % 16) - 7.5f, z = float(i / 16) - 4.5f;
        draw.model = glm::scale(glm::translate(glm::mat4(1), glm::vec3(x * .9f, -.6f + .1f * float(i % 5), z * .9f - 3)),
                                glm::vec3(.4f + .02f * float(i % 7)));
        draw.baseColor = {.2f + .1f * float(i % 5), .3f, .2f + .05f * float(i % 9), 1};   // Repeating materials.
        draw.metallic = float(i % 3) * .4f;
        draw.roughness = .3f + .1f * float(i % 4);
        draws.push_back(draw);
    }
    draws[5].model = glm::scale(draws[5].model, glm::vec3(-1, 1, 1));          // Mirrored: inverted winding.
    draws[9].outlineWidth = .05f;
    draws[9].outlineColor = {1, .8f, 0};
    draws[13].alphaMode = 1;
    draws[13].alphaCutoff = .5f;
    draws[13].baseColor.a = .8f;
    auto instances = std::make_shared<std::vector<glm::mat4>>();
    for (int i = 0; i < 6; ++i)
        instances->push_back(glm::scale(glm::translate(glm::mat4(1), glm::vec3(-3 + i, .2f, -1)), glm::vec3(.3f)));
    BasicDraw instanced;
    instanced.mesh = &cube;
    instanced.instanceModels = instances;
    instanced.previousInstanceModels = instances;
    instanced.baseColor = {.9f, .3f, .3f, 1};
    draws.push_back(instanced);
    BasicDraw pane;
    pane.mesh = &cube;
    pane.model = glm::scale(glm::translate(glm::mat4(1), glm::vec3(1, 0, 1)), glm::vec3(1.5f, 1.5f, .05f));
    pane.alphaMode = 2;
    pane.baseColor = {.4f, .6f, 1, .4f};
    draws.push_back(pane);

    BasicLighting lighting;
    lighting.shadowsEnabled = true;
    lighting.directionalDirection = glm::normalize(glm::vec3(-.4f, -1, -.3f));
    lighting.cameraPosition = {0, 3, 6};
    lighting.view = glm::lookAtRH(lighting.cameraPosition, glm::vec3(0, -.5f, -2), glm::vec3(0, 1, 0));
    BasicPointLight point;
    point.position = {0, 1.5f, -2};
    point.range = 6;
    point.intensity = 4;
    point.castsShadows = true;
    lighting.pointLights.push_back(point);
    const auto projection = glm::perspectiveRH_ZO(glm::radians(60.0f), 128.0f / 96.0f, 100.0f, 0.1f);

    // Converges VSM pages and caches, then returns the image and the frame's draw statistics. The first frame lights
    // from elsewhere so every mode re-plans and re-records its VSM pages instead of reusing the previous mode's.
    const auto sunDirection = lighting.directionalDirection;
    const auto render = [&](bool parallel, bool streaming)
    {
        renderer.SetParallelRecordingEnabled(parallel);
        renderer.SetObjectStreamingEnabled(streaming);
        std::uint32_t pageBatches = 0;
        for (int frame = 0; frame < 6; ++frame)
        {
            lighting.directionalDirection = frame == 0 ? glm::normalize(glm::vec3(.5f, -1, .2f)) : sunDirection;
            renderer.Render(projection * lighting.view, lighting, draws);
            pageBatches += renderer.GetFrameStats().virtualShadows.pageDrawBatches;
        }
        require(pageBatches > 0, "VSM recorded no caster pages");
        return std::pair{readPixels(renderer.GetColorTexture()), renderer.GetFrameStats()};
    };
    const auto [reference, referenceStats] = render(false, false);
    for (const auto [parallel, streaming] : {std::pair{true, true}, std::pair{true, false}, std::pair{false, true}})
    {
        const auto [image, stats] = render(parallel, streaming);
        const auto label = std::string(parallel ? "parallel" : "serial") + (streaming ? " streamed" : " bound") + " recording";
        if (parallel && PlutoGE::core::TaskPool::Shared().Concurrency() > 1)
            require(renderer.GetTimingStats().parallelRecording, label + " did not record in parallel");
        require(image == reference, label + " changed the rendered image");
        require(stats.geometryDraws == referenceStats.geometryDraws && stats.geometryDepthDraws == referenceStats.geometryDepthDraws &&
                stats.geometryInstances == referenceStats.geometryInstances && stats.pointShadowDraws == referenceStats.pointShadowDraws,
                label + " changed the recorded work");
    }
    renderer.SetParallelRecordingEnabled(true);
    renderer.SetObjectStreamingEnabled(true);
    renderer.Resize(width, height);
    std::cout << "Parallel and streamed recording match serial, bound recording" << std::endl;
}
