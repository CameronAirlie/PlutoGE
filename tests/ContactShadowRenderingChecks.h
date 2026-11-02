#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Directional contact shadows (ScreenSpaceContactShadows): a cube that casts no shadow-map shadow stands on a lit
// floor, so any shadow at its foot is the contact shadow's. They must darken the floor beside the cube on the side
// away from the light, leave a bare floor (which can only shadow itself) unchanged, and only ever darken.
template <class ReadPixels>
void CheckContactShadows(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    const auto require = [](bool condition, const std::string &message) {
        if (!condition) throw std::runtime_error(message);
    };
    constexpr std::uint32_t Width = 128, Height = 96;
    const auto previousWidth = renderer.GetWidth(), previousHeight = renderer.GetHeight();
    require(renderer.Resize(Width, Height), "Contact shadow viewport resize failed");
    constexpr std::array<BasicVertex, 24> cubeVertices = {{
        {{-.5f,-.5f,.5f},{0,0,1},{0,0}}, {{.5f,-.5f,.5f},{0,0,1},{1,0}}, {{.5f,.5f,.5f},{0,0,1},{1,1}}, {{-.5f,.5f,.5f},{0,0,1},{0,1}},
        {{.5f,-.5f,-.5f},{0,0,-1},{0,0}}, {{-.5f,-.5f,-.5f},{0,0,-1},{1,0}}, {{-.5f,.5f,-.5f},{0,0,-1},{1,1}}, {{.5f,.5f,-.5f},{0,0,-1},{0,1}},
        {{.5f,-.5f,.5f},{1,0,0},{0,0}}, {{.5f,-.5f,-.5f},{1,0,0},{1,0}}, {{.5f,.5f,-.5f},{1,0,0},{1,1}}, {{.5f,.5f,.5f},{1,0,0},{0,1}},
        {{-.5f,-.5f,-.5f},{-1,0,0},{0,0}}, {{-.5f,-.5f,.5f},{-1,0,0},{1,0}}, {{-.5f,.5f,.5f},{-1,0,0},{1,1}}, {{-.5f,.5f,-.5f},{-1,0,0},{0,1}},
        {{-.5f,.5f,.5f},{0,1,0},{0,0}}, {{.5f,.5f,.5f},{0,1,0},{1,0}}, {{.5f,.5f,-.5f},{0,1,0},{1,1}}, {{-.5f,.5f,-.5f},{0,1,0},{0,1}},
        {{-.5f,-.5f,-.5f},{0,-1,0},{0,0}}, {{.5f,-.5f,-.5f},{0,-1,0},{1,0}}, {{.5f,-.5f,.5f},{0,-1,0},{1,1}}, {{-.5f,-.5f,.5f},{0,-1,0},{0,1}}}};
    std::array<std::uint32_t, 36> cubeIndices{};
    for (std::uint32_t face = 0; face < 6; ++face)
    {
        const std::array<std::uint32_t, 6> quad{0, 1, 2, 0, 2, 3};
        for (std::uint32_t i = 0; i < 6; ++i) cubeIndices[face * 6 + i] = face * 4 + quad[i];
    }
    constexpr std::array<BasicVertex, 4> planeVertices = {{
        {{-1,0,-1},{0,1,0},{0,0}}, {{1,0,-1},{0,1,0},{1,0}}, {{1,0,1},{0,1,0},{1,1}}, {{-1,0,1},{0,1,0},{0,1}}}};
    constexpr std::array<std::uint32_t, 6> planeIndices{0,2,1, 0,3,2};
    auto cube = renderer.CreateMesh({cubeVertices, cubeIndices});
    auto plane = renderer.CreateMesh({planeVertices, planeIndices});

    BasicLighting lighting;
    lighting.shadowsEnabled = true;
    lighting.ambientIntensity = 0.05f;
    lighting.directionalIntensity = 3.0f;
    // Low from +x, so the contact shadow falls on the floor at the cube's -x side.
    lighting.directionalDirection = glm::normalize(glm::vec3(-1.0f, -0.6f, 0.0f));
    lighting.cameraPosition = {-0.2f, 1.1f, 1.5f};
    lighting.view = glm::lookAtRH(lighting.cameraPosition, glm::vec3(-0.3f, 0.1f, 0.0f), glm::vec3(0, 1, 0));
    const auto projection = glm::perspectiveRH_ZO(glm::radians(60.0f), float(Width) / float(Height), 100.0f, 0.1f);
    BasicDraw floor{.mesh = &plane, .model = glm::scale(glm::mat4(1), glm::vec3(6.0f))};
    BasicDraw block{.mesh = &cube, .model = glm::scale(glm::translate(glm::mat4(1), glm::vec3(0, 0.2f, 0)), glm::vec3(0.4f))};
    block.castsShadow = false;
    const auto render = [&](std::span<const BasicDraw> draws, float length)
    {
        lighting.contactShadowLength = length;
        std::vector<int> image;
        for (int frame = 0; frame < 4; ++frame)
        {
            renderer.Render(projection * lighting.view, lighting, draws);
            const auto pixels = readPixels(renderer.GetColorTexture());
            image.assign(pixels.size(), 0);
            std::transform(pixels.begin(), pixels.end(), image.begin(), [](auto value) { return static_cast<int>(value); });
        }
        return image;
    };
    const std::array scene{floor, block};
    const auto without = render(scene, 0.0f), with = render(scene, 0.4f);
    const auto bareWithout = render(std::span(&floor, 1), 0.0f), bareWith = render(std::span(&floor, 1), 0.4f);
    require(with.size() == std::size_t(Width) * Height * 4 && with.size() == without.size(), "Contact shadow readback differs");
    if (const char *directory = std::getenv("PLUTO_CONTACT_SHADOW_CAPTURE_DIR"))
        for (const auto &[name, image] : {std::pair{"without", &without}, std::pair{"with", &with}})
        {
            std::ofstream out(std::filesystem::path(directory) / (std::string(name) + ".ppm"), std::ios::binary);
            out << "P6\n" << Width << ' ' << Height << "\n255\n";
            for (std::size_t i = 0; i < image->size(); ++i)
                if (i % 4 != 3) out.put(static_cast<char>((*image)[i]));
        }
    std::size_t darkened = 0;
    int bareDifference = 0;
    for (std::size_t i = 0; i < with.size(); i += 4)
    {
        require(with[i + 1] <= without[i + 1] + 1, "Contact shadows brightened a surface");
        darkened += without[i + 1] > 40 && with[i + 1] * 10 < without[i + 1] * 7;
        bareDifference = std::max(bareDifference, std::abs(bareWith[i + 1] - bareWithout[i + 1]));
    }
    if (darkened <= 40 || bareDifference > 1)
        std::cerr << "Contact shadows: " << darkened << " darkened pixels, bare floor difference " << bareDifference << '\n';
    require(darkened > 40, "Contact shadows did not shadow the floor beside a cube");
    require(bareDifference <= 1, "Contact shadows shadowed a bare floor (self-shadowing)");
    // A bare floor seen at a grazing angle, lit from beyond it so the rays towards the light run deeper into the
    // screen: its depth changes by far more than the bias across a texel and must not shadow itself.
    lighting.directionalDirection = glm::normalize(glm::vec3(0.0f, -0.5f, 1.0f));
    lighting.cameraPosition = {0.0f, 0.12f, 5.5f};
    lighting.view = glm::lookAtRH(lighting.cameraPosition, glm::vec3(0.0f, 0.0f, -6.0f), glm::vec3(0, 1, 0));
    const auto grazingWithout = render(std::span(&floor, 1), 0.0f), grazingWith = render(std::span(&floor, 1), 0.4f);
    int grazingDifference = 0;
    for (std::size_t i = 0; i < grazingWith.size(); i += 4)
        grazingDifference = std::max(grazingDifference, std::abs(grazingWith[i + 1] - grazingWithout[i + 1]));
    if (grazingDifference > 1)
        std::cerr << "Contact shadows: grazing bare floor difference " << grazingDifference << '\n';
    require(grazingDifference <= 1, "Contact shadows shadowed a bare floor seen at a grazing angle");
    std::cout << "Contact shadows: " << darkened << " darkened pixels beside the cube, bare floor unchanged\n";
    require(renderer.Resize(previousWidth, previousHeight), "Contact shadow viewport restore failed");
}
