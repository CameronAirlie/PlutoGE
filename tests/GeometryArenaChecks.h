#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>

// Rigid meshes in a geometry arena share buffers, so virtual shadow page draws of different meshes, rigid or instanced,
// batch into one indirect draw. The image must equal unbatched page drawing, and a scene of many distinct meshes must
// need far fewer page batches.
template<class ReadPixels>
void CheckGeometryArena(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels)
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
    constexpr std::array<BasicVertex, 4> planeVertices = {{
        {{{-1,0,-1}},{{0,1,0}},{{0,0}}}, {{{1,0,-1}},{{0,1,0}},{{1,0}}},
        {{{1,0,1}},{{0,1,0}},{{1,1}}}, {{{-1,0,1}},{{0,1,0}},{{0,1}}}}};
    constexpr std::array<std::uint32_t, 6> planeIndices{0,2,1, 0,3,2};

    BasicLighting lighting;
    lighting.shadowsEnabled = true;
    lighting.directionalDirection = glm::normalize(glm::vec3(-.4f, -1, -.3f));
    lighting.cameraPosition = {0, 3, 6};
    lighting.view = glm::lookAtRH(lighting.cameraPosition, glm::vec3(0, -.5f, -2), glm::vec3(0, 1, 0));
    const auto projection = glm::perspectiveRH_ZO(glm::radians(60.0f), 128.0f / 96.0f, 100.0f, 0.1f);

    // Every caster is a mesh of its own, as in a level built from distinct props. Returns the image and the page
    // batches recorded while the pages converged.
    const auto render = [&](bool arena, bool batched)
    {
        renderer.SetGeometryArenaEnabled(arena);
        lighting.virtualShadowBatching = batched;
        auto plane = renderer.CreateMesh({planeVertices, planeIndices});
        std::vector<BasicMesh> meshes;
        meshes.reserve(48);
        for (int i = 0; i < 48; ++i)
            meshes.push_back(renderer.CreateMesh({vertices, indices}));
        std::vector<BasicDraw> draws;
        BasicDraw ground;
        ground.mesh = &plane;
        ground.model = glm::scale(glm::translate(glm::mat4(1), glm::vec3(0, -1, 0)), glm::vec3(8));
        draws.push_back(ground);
        for (int i = 0; i < 48; ++i)
        {
            BasicDraw draw;
            draw.mesh = &meshes[i];
            const float x = float(i % 8) - 3.5f, z = float(i / 8) - 3;
            draw.model = glm::scale(glm::translate(glm::mat4(1), glm::vec3(x, -.6f + .1f * float(i % 4), z - 2)), glm::vec3(.45f));
            draw.baseColor = {.2f + .1f * float(i % 5), .4f, .3f, 1};
            if (i % 7 == 3)
            {
                // Instanced casters interleaved with rigid ones.
                auto instances = std::make_shared<std::vector<glm::mat4>>();
                for (int instance = 0; instance < 5; ++instance)
                    instances->push_back(glm::translate(draw.model, glm::vec3(0, 1.2f + .6f * float(instance), 0)));
                draw.instanceModels = instances;
                draw.previousInstanceModels = instances;
            }
            draws.push_back(draw);
        }
        std::uint32_t pageBatches = 0;
        for (int frame = 0; frame < 6; ++frame)
        {
            // A different first light direction makes each mode record its pages afresh.
            lighting.directionalDirection = frame == 0 ? glm::normalize(glm::vec3(.5f, -1, .2f)) : glm::normalize(glm::vec3(-.4f, -1, -.3f));
            renderer.Render(projection * lighting.view, lighting, draws);
            pageBatches += renderer.GetFrameStats().virtualShadows.pageDrawBatches;
        }
        return std::pair{readPixels(renderer.GetColorTexture()), pageBatches};
    };
    const auto [unbatched, unbatchedDraws] = render(false, false);
    const auto [separate, separateBatches] = render(false, true);
    const auto [shared, sharedBatches] = render(true, true);
    require(separateBatches > 0, "Geometry arena check recorded no shadow pages");
    require(separate == unbatched, "Batched shadow page draws changed the rendered image");
    require(shared == unbatched, "Arena meshes changed the rendered image");
    require(sharedBatches * 4 <= separateBatches, "Arena meshes did not batch shadow page draws (" +
            std::to_string(sharedBatches) + " batches, " + std::to_string(separateBatches) + " without the arena)");
    renderer.SetGeometryArenaEnabled(true);
    renderer.Resize(width, height);
    (void)unbatchedDraws;
    std::cout << "Geometry arena: identical image, shadow page batches " << separateBatches << " -> " << sharedBatches << std::endl;
}
