#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/Camera.h"
#include "../engine/render/src/DirectionalShadowCascades.h"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>

// The directional shadow mask (DirectionalShadowMask.h) filters the directional shadow once per pixel of the coverage
// depth, from receivers rebuilt from that depth, and prepassed surfaces read it. Planar receivers rebuild exactly, so
// a scene of boxes on a ground must render as the surfaces' own filter does, apart from silhouette pixels, with
// virtual shadow maps and with cascades. The shadows must be real (the comparison would otherwise pass trivially), and
// the mask must actually be in use.
template<class ReadPixels>
void CheckDirectionalShadowMask(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    const auto require = [](bool condition, const std::string &message) {
        if (!condition) throw std::runtime_error(message);
    };
    const auto width = renderer.GetWidth(), height = renderer.GetHeight();
    constexpr std::uint32_t Width = 160, Height = 120;
    renderer.Resize(Width, Height);
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
    auto cube = renderer.CreateMesh({vertices, indices});
    auto plane = renderer.CreateMesh({planeVertices, planeIndices});

    std::vector<BasicDraw> draws;
    BasicDraw ground;
    ground.mesh = &plane;
    ground.model = glm::scale(glm::translate(glm::mat4(1), glm::vec3(0, -1, 0)), glm::vec3(8));
    draws.push_back(ground);
    for (int i = 0; i < 6; ++i)
    {
        BasicDraw box;
        box.mesh = &cube;
        // Some boxes turned, so receivers face the light at several slopes.
        box.model = glm::rotate(glm::translate(glm::mat4(1), glm::vec3(float(i % 3) * 1.5f - 1.5f, -0.4f, float(i / 3) * 1.6f - 2.6f)),
                                glm::radians(20.0f * float(i)), glm::vec3(0, 1, 0)) * glm::scale(glm::mat4(1), glm::vec3(0.7f, 1.2f, 0.7f));
        box.baseColor = {0.7f, 0.65f, 0.6f, 1};
        draws.push_back(box);
    }

    BasicLighting lighting;
    lighting.shadowsEnabled = true;
    lighting.directionalDirection = glm::normalize(glm::vec3(-0.6f, -1.0f, -0.4f));
    lighting.directionalIntensity = 3.0f;
    lighting.ambientIntensity = 0.2f;
    lighting.cameraPosition = {0, 3.5f, 4.5f};
    lighting.view = glm::lookAtRH(lighting.cameraPosition, glm::vec3(0, -0.8f, -1.5f), glm::vec3(0, 1, 0));
    const auto projection = glm::perspectiveRH_ZO(glm::radians(60.0f), float(Width) / Height, 100.0f, 0.1f);

    for (const auto method : {ShadowMethod::Virtual, ShadowMethod::Cascaded})
    {
        const char *name = method == ShadowMethod::Virtual ? "virtual shadow maps" : "cascades";
        lighting.shadowMethod = method;
        if (method == ShadowMethod::Cascaded)
        {
            // One cascade fitted to the view, as RhiSceneRenderer fits them (Vulkan: the map's rows flip).
            lighting.shadowCascadeCount = 1;
            lighting.shadowResolution = 1024;
            lighting.shadowFlipY = true;
            CameraData camera{lighting.view, projection, 0.1f, 100.0f};
            const auto cascade = BuildDirectionalShadowProjection(camera, lighting, 0.1f, 20.0f, 20.0f, lighting.shadowResolution);
            lighting.shadowMatrices[0] = cascade.matrix;
            lighting.shadowCascadeMetrics[0] = {cascade.worldTexelSize, cascade.depthRange, 0.0f, 0.0f};
            lighting.shadowCascadeSplits[0] = 20.0f;
        }
        const auto render = [&](bool mask, bool shadows)
        {
            renderer.SetDirectionalShadowMask(mask);
            auto frameLighting = lighting;
            frameLighting.shadowsEnabled = shadows;
            // Virtual shadow pages converge over a few frames.
            for (int frame = 0; frame < 6; ++frame)
                renderer.Render(projection * frameLighting.view, frameLighting, draws);
            return std::pair{readPixels(renderer.GetColorTexture()), renderer.GetFrameStats().directionalShadowMask};
        };
        const auto [unshadowed, unused] = render(false, false);
        const auto [filtered, filteredMask] = render(false, true);
        const auto [masked, maskUsed] = render(true, true);
        (void)unused;
        require(!filteredMask, std::string(name) + ": the mask must be off when disabled");
        require(maskUsed, std::string(name) + ": prepassed surfaces must read the directional shadow mask");
        double shadowed = 0.0, difference = 0.0;
        std::size_t different = 0;
        for (std::size_t i = 0; i < filtered.size(); ++i)
        {
            if (i % 4 == 3) continue;
            shadowed += std::abs(int(unshadowed[i]) - int(filtered[i]));
            const int delta = std::abs(int(masked[i]) - int(filtered[i]));
            difference += delta;
            if (delta > 16) ++different;
        }
        const double channels = double(Width) * Height * 3.0;
        std::cout << "Directional shadow mask (" << name << "): shadows " << shadowed / channels << ", mask against filter "
                  << difference / channels << " mean, " << 100.0 * double(different) / channels << "% beyond 16\n";
        require(shadowed / channels > 1.5, std::string(name) + ": the fixture must cast visible shadows");
        require(difference / channels < 0.6, std::string(name) + ": the mask must match the surfaces' own filter");
        require(double(different) / channels < 0.015, std::string(name) + ": only silhouette pixels may differ");
    }
    renderer.SetDirectionalShadowMask(true);
    renderer.Resize(width, height);
}
