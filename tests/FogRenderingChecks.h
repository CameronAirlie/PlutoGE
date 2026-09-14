#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

template <class ReadPixels>
void CheckFogRendering(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels, unsigned divisor = 1)
{
    using namespace PlutoGE::render;
    renderer.Resize(33, 33);
    BasicLighting lighting;
    lighting.view = glm::mat4(1);
    lighting.cameraPosition = {0, 0, 0};
    lighting.ambientIntensity = 0;
    lighting.directionalIntensity = 0;
    lighting.shadowsEnabled = false;
    glm::mat4 projection(0);
    projection[0][0] = projection[1][1] = 1;
    projection[2][2] = .1f / (2000.f - .1f);
    projection[2][3] = -1;
    projection[3][2] = 2000.f * .1f / (2000.f - .1f);
    BasicPostProcessEffect fog{BasicPostProcessEffectType::VolumetricFog};
    fog.volumetricResolutionDivisor = divisor;
    fog.quality = 16;
    fog.parameters[0] = {1, 1, 1, .05f};
    fog.parameters[1] = {0, 0, 1000, 0};
    fog.parameters[2] = {0, 1, 1, 1};
    fog.parameters[3].x = 1;
    constexpr std::array<BasicVertex, 4> vertices = {{
        {{{-1,-1,0}}, {{0,0,1}}, {{0,0}}},
        {{{1,-1,0}}, {{0,0,1}}, {{1,0}}},
        {{{1,1,0}}, {{0,0,1}}, {{1,1}}},
        {{{-1,1,0}}, {{0,0,1}}, {{0,1}}}
    }};
    constexpr std::array<std::uint32_t, 6> indices{0,1,2,0,2,3};
    auto mesh = renderer.CreateMesh({vertices, indices});
    BasicDraw draw;
    draw.mesh = &mesh;
    draw.emission = {1, 1, 1};
    const auto sample = [&](bool surface)
    {
        renderer.Render(projection * lighting.view, lighting,
            surface ? std::span(&draw, 1) : std::span<BasicDraw>{}, std::span(&fog, 1));
        const auto pixels = readPixels(renderer.GetColorTexture());
        return static_cast<int>(pixels[(16 * 33 + 16) * 4]) / 255.0f;
    };
    const auto expect = [](float actual, float expected, const char *message)
    {
        if (std::abs(actual - expected) > .015f)
        {
            std::cerr << message << ": " << actual << " expected " << expected << '\n';
            throw std::runtime_error(message);
        }
    };
    draw.model = glm::translate(glm::mat4(1), glm::vec3(0,0,-2));
    expect(sample(true), std::exp(-.1f), "Near fog extinction");
    draw.model = glm::translate(glm::mat4(1), glm::vec3(0,0,-20));
    expect(sample(true), std::exp(-1.f), "Distant fog extinction");
    fog.parameters[1] = {1, -100, 1000, 1};
    fog.parameters[0].w = 1;
    lighting.ambientIntensity = 1;
    expect(sample(false), .04f, "Sky above the fog layer must stay clear");
    fog.parameters[1].y = 0;
    fog.parameters[0].w = .1f;
    lighting.view = glm::lookAtRH(glm::vec3(0), glm::vec3(0,1,0), glm::vec3(0,0,1));
    const float thinLayer = 1.f - .96f * std::exp(-.1f);
    expect(sample(false), thinLayer, "Long upward ray must integrate thin ground fog");
    fog.quality = 64;
    expect(sample(false), thinLayer, "Fog extinction must not depend on step count");
    lighting.view = glm::mat4(1);
    fog.parameters[1].x = 0;
    lighting.ambientIntensity = 0;
    expect(sample(false), 0, "Unlit fog must not emit ambient light");
    lighting.directionalIntensity = 1;
    lighting.directionalColor = {1,1,1};
    expect(sample(false), 1.f / (4.f * 3.14159265f), "Directional fog phase must be normalized");
    lighting.directionalIntensity = 0;
    lighting.ambientIntensity = 1;
    fog.parameters[2].w = .25f;
    expect(sample(false), .04f * .75f + .25f, "Sky must respect maximum fog opacity");
    std::cout << "Fog distance, height, integration and lighting checks passed\n";
    if (divisor == 1)
        CheckFogRendering(renderer, readPixels, 2);
}
