#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include "../engine/render/src/SurfaceFeatures.h"
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>

// Vulkan geometry pipelines specialise BasicLit to the lighting paths the frame and material use (SurfaceFeatures.h).
// A path is compiled out only where its data would skip it, so every scene must render the same bytes with
// specialisation off. Each scene also checks that the frame's features narrowed as expected, so the comparison cannot
// pass merely because nothing was specialised.
template<class ReadPixels>
void CheckSurfaceSpecialization(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels)
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
    auto cube = renderer.CreateMesh({vertices, indices});
    auto plane = renderer.CreateMesh({planeVertices, planeIndices});

    // A ground, plain cubes, an instanced column, and one cube each with clear coat and sheen.
    std::vector<BasicDraw> draws;
    BasicDraw ground;
    ground.mesh = &plane;
    ground.model = glm::scale(glm::translate(glm::mat4(1), glm::vec3(0, -1, 0)), glm::vec3(6));
    ground.roughness = .6f;
    draws.push_back(ground);
    for (int i = 0; i < 6; ++i)
    {
        BasicDraw draw;
        draw.mesh = &cube;
        draw.model = glm::scale(glm::translate(glm::mat4(1), glm::vec3(float(i % 3) * 1.4f - 1.4f, -.5f, float(i / 3) * 1.4f - 2.5f)),
                                glm::vec3(.8f));
        draw.baseColor = {.3f + .1f * float(i), .45f, .35f, 1};
        draw.roughness = .35f + .1f * float(i % 3);
        if (i == 1) { draw.clearCoat = 1.0f; draw.clearCoatRoughness = .1f; }
        if (i == 4) { draw.sheenColor = {.6f, .5f, .4f}; draw.sheenRoughness = .4f; }
        if (i == 5)
        {
            auto instances = std::make_shared<std::vector<glm::mat4>>();
            for (int instance = 0; instance < 4; ++instance)
                instances->push_back(glm::translate(draw.model, glm::vec3(0, 1.1f * float(instance), 0)));
            draw.instanceModels = instances;
            draw.previousInstanceModels = instances;
        }
        draws.push_back(draw);
    }

    BasicLighting base;
    base.directionalDirection = glm::normalize(glm::vec3(-.4f, -1, -.3f));
    base.directionalIntensity = 3.0f;
    base.cameraPosition = {0, 3, 5};
    base.view = glm::lookAtRH(base.cameraPosition, glm::vec3(0, -.5f, -2), glm::vec3(0, 1, 0));
    const auto projection = glm::perspectiveRH_ZO(glm::radians(60.0f), 128.0f / 96.0f, 100.0f, 0.1f);

    struct Scene
    {
        const char *name;
        BasicLighting lighting;
        std::uint32_t expected; // the frame's features (SurfaceLayers is always kept at frame level)
    };
    std::vector<Scene> scenes;
    {
        auto lighting = base;
        lighting.shadowsEnabled = true;
        lighting.shadowMethod = ShadowMethod::Virtual;
        scenes.push_back({"virtual shadows", lighting, SurfaceLayers | SurfaceVirtualShadows});
    }
    {
        auto lighting = base;
        lighting.shadowsEnabled = true;
        lighting.shadowMethod = ShadowMethod::Cascaded;
        lighting.shadowCascadeCount = 2;
        scenes.push_back({"cascaded shadows", lighting, SurfaceLayers | SurfaceCascadedShadows});
    }
    {
        auto lighting = base;
        lighting.shadowsEnabled = false;
        scenes.push_back({"no shadows", lighting, SurfaceLayers});
    }
    {
        auto lighting = base;
        lighting.shadowsEnabled = true;
        lighting.shadowMethod = ShadowMethod::Virtual;
        BasicPointLight point;
        point.position = {1.2f, .4f, -1.5f};
        point.color = {1, .6f, .3f};
        point.intensity = 6;
        point.range = 6;
        lighting.pointLights.push_back(point);
        BasicLighting::SpotLight spot;
        spot.light.position = {-1.5f, 1.8f, -1};
        spot.light.intensity = 10;
        spot.light.range = 8;
        spot.direction = glm::normalize(glm::vec3(.3f, -1, -.4f));
        lighting.spotLights.push_back(spot);
        scenes.push_back({"local lights", lighting, SurfaceLayers | SurfaceVirtualShadows | SurfaceLocalLights});
    }

    for (const auto &scene : scenes)
    {
        const auto render = [&](bool specialised)
        {
            renderer.SetSurfaceSpecialization(specialised);
            // Virtual shadow pages and temporal state converge over a few frames.
            for (int frame = 0; frame < 4; ++frame)
                renderer.Render(projection * scene.lighting.view, scene.lighting, draws);
            return std::pair{readPixels(renderer.GetColorTexture()), renderer.GetFrameStats().surfaceFeatures};
        };
        const auto [general, generalFeatures] = render(false);
        const auto [specialised, features] = render(true);
        require((generalFeatures & AllSurfaceFeatures) == AllSurfaceFeatures,
                std::string("Unspecialised frame kept only some surface features (") + scene.name + ")");
        require(features == scene.expected, std::string("Surface features for ") + scene.name + " were " +
                std::to_string(features) + ", expected " + std::to_string(scene.expected));
        require(specialised == general, std::string("Surface specialisation changed the image (") + scene.name + ")");
    }
    renderer.SetSurfaceSpecialization(true);
    renderer.Resize(width, height);
    std::cout << "Surface specialisation: " << scenes.size() << " scenes render identically" << std::endl;
}
