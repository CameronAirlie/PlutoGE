#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Rectangle lights (LtcAreaLights.slang). A small rectangle far above a floor lights it as a point light of the same
// intensity does; turned away it lights nothing (one-sided); and on a glossy floor a long, thin rectangle's
// reflection is long in the direction the rectangle is, and turns with it.
template <class ReadPixels>
void CheckRectLights(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    const auto require = [](bool condition, const std::string &message) {
        if (!condition) throw std::runtime_error(message);
    };
    constexpr std::array<BasicVertex, 4> planeVertices = {{
        {{-1, 0, -1}, {0, 1, 0}, {0, 0}}, {{1, 0, -1}, {0, 1, 0}, {1, 0}}, {{1, 0, 1}, {0, 1, 0}, {1, 1}}, {{-1, 0, 1}, {0, 1, 0}, {0, 1}}}};
    constexpr std::array<std::uint32_t, 6> planeIndices{0, 2, 1, 0, 3, 2};
    auto plane = renderer.CreateMesh({planeVertices, planeIndices});
    BasicDraw floor{.mesh = &plane, .model = glm::scale(glm::mat4(1), glm::vec3(20.0f))};
    floor.baseColor = {0.5f, 0.5f, 0.5f, 1.0f};
    floor.roughness = 1.0f;

    BasicLighting lighting;
    lighting.shadowsEnabled = false;
    lighting.contactShadowLength = 0.0f;
    lighting.ambientIntensity = 0.0f;
    lighting.directionalIntensity = 0.0f;
    const float aspect = float(renderer.GetWidth()) / float(renderer.GetHeight());
    const auto projection = glm::perspectiveRH_ZO(glm::radians(60.0f), aspect, 100.0f, 0.1f);
    const auto image = [&]
    {
        renderer.Render(projection * lighting.view, lighting, std::span(&floor, 1));
        return readPixels(renderer.GetColorTexture());
    };
    const auto centre = [&]
    {
        const auto pixels = image();
        const auto at = (renderer.GetHeight() / 2 * renderer.GetWidth() + renderer.GetWidth() / 2) * 4;
        return int(pixels[at + 1]);
    };

    // Looking straight down at the floor below the light.
    lighting.cameraPosition = {0.0f, 6.0f, 0.0f};
    lighting.view = glm::lookAtRH(lighting.cameraPosition, glm::vec3(0.0f), glm::vec3(0, 0, -1));
    BasicPointLight emitter{.position = {0.0f, 3.0f, 0.0f}, .range = 20.0f, .color = glm::vec3(1.0f), .intensity = 12.0f};
    lighting.pointLights = {emitter};
    const int point = centre();
    lighting.pointLights.clear();
    lighting.rectLights = {{emitter, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 0.1f, 0.1f}};
    const int rect = centre();
    lighting.rectLights[0].direction = {0.0f, 1.0f, 0.0f};
    const int away = centre();
    std::cout << "Rect lights: point " << point << ", small rectangle " << rect << ", turned away " << away << '\n';
    require(point > 30, "Rect light scene is too dark to compare");
    require(std::abs(rect - point) <= 3, "A small rectangle far away did not light like a point of its intensity");
    require(away <= 1, "A rectangle lit the side it faces away from");

    // A glossy floor seen at an angle, reflecting a long thin rectangle.
    floor.roughness = 0.25f;
    lighting.cameraPosition = {0.0f, 2.0f, 4.0f};
    lighting.view = glm::lookAtRH(lighting.cameraPosition, glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0, 1, 0));
    const auto highlight = [&](glm::vec3 right)
    {
        lighting.rectLights = {{emitter, {0.0f, -1.0f, 0.0f}, right, 3.0f, 0.2f}};
        lighting.rectLights[0].light.position = {0.0f, 2.0f, -2.0f};
        lighting.rectLights[0].light.intensity = 20.0f;
        const auto pixels = image();
        int peak = 0;
        for (std::size_t i = 0; i < pixels.size(); i += 4) peak = std::max(peak, int(pixels[i + 1]));
        int left = int(renderer.GetWidth()), rightEdge = -1, top = int(renderer.GetHeight()), bottom = -1;
        for (int y = 0; y < int(renderer.GetHeight()); ++y)
            for (int x = 0; x < int(renderer.GetWidth()); ++x)
                if (int(pixels[(std::size_t(y) * renderer.GetWidth() + x) * 4 + 1]) * 2 >= peak)
                {
                    left = std::min(left, x); rightEdge = std::max(rightEdge, x);
                    top = std::min(top, y); bottom = std::max(bottom, y);
                }
        return std::array<int, 3>{rightEdge - left + 1, bottom - top + 1, peak};
    };
    const auto across = highlight({1.0f, 0.0f, 0.0f});
    const auto along = highlight({0.0f, 0.0f, 1.0f});
    std::cout << "Rect lights: highlight across the view " << across[0] << " x " << across[1] << " (peak " << across[2]
              << "), turned " << along[0] << " x " << along[1] << " (peak " << along[2] << ")\n";
    require(across[2] > 60 && along[2] > 60, "Rectangle reflection is too dim to measure");
    require(across[0] > across[1] && across[0] > along[0] * 2,
            "A long rectangle's reflection did not stretch along the rectangle");
}
