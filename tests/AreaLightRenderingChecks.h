#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Spherical and tube lights (SurfaceBrdf.slang brdfAreaLight): a point light's highlight on a glossy floor must widen
// when the light has a source radius, and stretch along a tube's axis when it has a length. Only specular changes, so
// the comparison uses a black dielectric floor's reflection alone.
template <class ReadPixels>
void CheckAreaLights(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    const auto require = [](bool condition, const std::string &message) {
        if (!condition) throw std::runtime_error(message);
    };
    constexpr std::uint32_t Width = 128, Height = 96;
    const auto previousWidth = renderer.GetWidth(), previousHeight = renderer.GetHeight();
    require(renderer.Resize(Width, Height), "Area light viewport resize failed");
    constexpr std::array<BasicVertex, 4> planeVertices = {{
        {{-1,0,-1},{0,1,0},{0,0}}, {{1,0,-1},{0,1,0},{1,0}}, {{1,0,1},{0,1,0},{1,1}}, {{-1,0,1},{0,1,0},{0,1}}}};
    constexpr std::array<std::uint32_t, 6> planeIndices{0,2,1, 0,3,2};
    auto plane = renderer.CreateMesh({planeVertices, planeIndices});
    BasicDraw floor{.mesh = &plane, .model = glm::scale(glm::mat4(1), glm::vec3(4.0f)), .baseColor = {0, 0, 0, 1},
                    .metallic = 0.0f, .roughness = 0.3f};
    BasicLighting lighting;
    lighting.ambientIntensity = lighting.directionalIntensity = 0;
    // Straight above the floor, seen from above: the highlight is centred and its spread is the light's shape's.
    lighting.cameraPosition = {0.0f, 2.0f, 0.01f};
    lighting.view = glm::lookAtRH(lighting.cameraPosition, glm::vec3(0.0f), glm::vec3(0, 0, -1));
    const auto projection = glm::perspectiveRH_ZO(glm::radians(60.0f), float(Width) / float(Height), 100.0f, 0.1f);
    struct Highlight { std::size_t pixels = 0; int width = 0, height = 0; };
    const auto measure = [&](float radius, float length)
    {
        BasicPointLight light{.position = {0.0f, 1.0f, 0.0f}, .range = 20.0f, .color = {1, 1, 1}, .intensity = 2.0f};
        light.sourceRadius = radius;
        light.sourceLength = length;
        light.sourceAxis = {1.0f, 0.0f, 0.0f};
        lighting.pointLights = {light};
        std::vector<int> image;
        for (int frame = 0; frame < 2; ++frame)
        {
            renderer.Render(projection * lighting.view, lighting, std::span(&floor, 1));
            const auto pixels = readPixels(renderer.GetColorTexture());
            image.assign(pixels.size(), 0);
            std::transform(pixels.begin(), pixels.end(), image.begin(), [](auto value) { return static_cast<int>(value); });
        }
        // The highlight's extent at half its own peak: a larger source spreads the same light more thinly.
        int peak = 0;
        for (std::size_t i = 1; i < image.size(); i += 4) peak = std::max(peak, image[i]);
        Highlight highlight;
        int minimumX = Width, maximumX = -1, minimumY = Height, maximumY = -1;
        for (std::uint32_t y = 0; y < Height; ++y)
            for (std::uint32_t x = 0; x < Width; ++x)
                if (peak > 4 && image[(y * Width + x) * 4 + 1] * 2 >= peak)
                {
                    ++highlight.pixels;
                    minimumX = std::min(minimumX, int(x)); maximumX = std::max(maximumX, int(x));
                    minimumY = std::min(minimumY, int(y)); maximumY = std::max(maximumY, int(y));
                }
        highlight.width = maximumX - minimumX + 1;
        highlight.height = maximumY - minimumY + 1;
        return highlight;
    };
    const auto point = measure(0.0f, 0.0f), sphere = measure(0.25f, 0.0f), tube = measure(0.05f, 1.2f);
    std::cout << "Area lights: highlight " << point.pixels << " pixels (point), " << sphere.pixels << " (sphere), tube "
              << tube.width << " x " << tube.height << '\n';
    require(point.pixels > 0, "A point light left no highlight on a glossy floor");
    require(sphere.pixels > point.pixels * 3 / 2, "A spherical light did not widen its highlight");
    require(tube.width > tube.height * 2 && tube.width > point.width * 2, "A tube light's highlight does not follow its axis");
    require(renderer.Resize(previousWidth, previousHeight), "Area light viewport restore failed");
}
