#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

// Surface wetness (BasicLighting::weather, SurfaceWetness.slang). A rough grey quad lit by ambient light only: soaked,
// it must darken where it faces the sky and barely change facing away. A rough metal quad must not darken.
// Dry (wetness 0) must render exactly as before.
template <class ReadPixels>
void CheckSurfaceWetness(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    const auto require = [](bool condition, const std::string &message) {
        if (!condition) throw std::runtime_error(message);
    };
    // A quad facing the camera (+Z) whose shading normal we choose, tilted toward the camera so it is shaded as a front
    // face: mostly up (+Y) faces the sky, mostly down (-Y) does not.
    const auto quad = [&](const std::array<float, 3> &normal)
    {
        std::array<BasicVertex, 4> vertices{};
        const std::array<std::array<float, 3>, 4> positions{{{-.8f, -.8f, .5f}, {.8f, -.8f, .5f}, {.8f, .8f, .5f}, {-.8f, .8f, .5f}}};
        for (unsigned i = 0; i < 4; ++i)
        {
            vertices[i].position = positions[i];
            vertices[i].normal = normal;
            vertices[i].tangent = {1, 0, 0, 1};
            vertices[i].uv = {.5f, .5f};
        }
        constexpr std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
        return renderer.CreateMesh({vertices, indices});
    };
    auto facingSky = quad({0, 0.8f, 0.6f});
    auto facingDown = quad({0, -0.8f, 0.6f});

    BasicLighting lighting;
    lighting.cameraPosition = {0, 0, 2};
    lighting.shadowsEnabled = false;
    lighting.contactShadowLength = 0.0f;
    lighting.ambientIntensity = 0.8f;
    lighting.directionalIntensity = 0.0f;
    BasicDraw draw;
    draw.twoSided = true;
    draw.baseColor = {0.5f, 0.5f, 0.5f, 1.0f};
    draw.roughness = 0.9f;
    const auto sample = [&](BasicMesh &mesh, float wetness, float metallic)
    {
        draw.mesh = &mesh;
        draw.metallic = metallic;
        lighting.weather.wetness = wetness;
        renderer.Render(glm::mat4(1), lighting, std::span(&draw, 1));
        const auto image = readPixels(renderer.GetColorTexture());
        const auto at = (renderer.GetHeight() / 2 * renderer.GetWidth() + renderer.GetWidth() / 2) * 4;
        return int(image[at]) + int(image[at + 1]) + int(image[at + 2]);
    };

    const int dry = sample(facingSky, 0.0f, 0.0f);
    const int dryAgain = sample(facingSky, 0.0f, 0.0f);
    const int soaked = sample(facingSky, 1.0f, 0.0f);
    const int damp = sample(facingSky, 0.4f, 0.0f);
    const int underDry = sample(facingDown, 0.0f, 0.0f);
    const int underSoaked = sample(facingDown, 1.0f, 0.0f);
    const int metalDry = sample(facingSky, 0.0f, 1.0f);
    const int metalSoaked = sample(facingSky, 1.0f, 1.0f);
    std::cout << "Surface wetness: sky-facing dry " << dry << ", damp " << damp << ", soaked " << soaked
              << "; facing down dry " << underDry << ", soaked " << underSoaked << "; metal dry " << metalDry
              << ", soaked " << metalSoaked << '\n';
    require(dry > 60, "Wetness scene has too little light to measure");
    require(dry == dryAgain, "A dry render is not repeatable");
    require(soaked * 10 < dry * 8, "A soaked rough surface facing the sky did not darken");
    require(damp < dry && damp > soaked, "Wetness does not darken in proportion");
    require(std::abs(underSoaked - underDry) * 20 <= underDry + 20, "A surface facing away from the sky got wet");
    require(std::abs(metalSoaked - metalDry) * 10 <= metalDry + 10, "Wetness darkened a metal surface");
}
