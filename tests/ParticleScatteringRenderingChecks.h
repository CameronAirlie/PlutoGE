#pragma once
#include "PlutoGE/render/BasicRenderer.h"

#include <array>
#include <cmath>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>

#include <glm/gtc/matrix_transform.hpp>

// Scattering particles (assets::ParticleLighting::Scattering, Particles.slang scatteringLighting): specks lit as the
// fog's medium is. Under a roof the sun's shadow map darkens them; in the open they are lit; looking toward the sun a
// forward-scattering speck outshines the same speck seen with the sun behind the camera.
template <class ReadPixels>
void CheckParticleScattering(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    const auto require = [](bool ok, const std::string &message) {
        if (!ok)
            throw std::runtime_error(message);
    };
    constexpr int width = 96, height = 64;
    const auto previousWidth = renderer.GetWidth(), previousHeight = renderer.GetHeight();
    renderer.Resize(width, height);

    BasicLighting lighting;
    lighting.cameraPosition = {0, 1, 0};
    lighting.view = glm::lookAtRH(lighting.cameraPosition, glm::vec3(0, 1, -1), glm::vec3(0, 1, 0));
    lighting.ambientIntensity = 0;
    lighting.directionalIntensity = 0.25f;
    lighting.directionalColor = {1, 1, 1};
    lighting.directionalDirection = {0, -1, 0};
    // Virtual shadow maps (the default) place themselves; cascades would need the scene renderer's matrices.
    lighting.shadowsEnabled = true;
    lighting.shadowResolution = 512;
    lighting.shadowCascadeCount = 1;
    lighting.shadowCascadeSplits = glm::vec4(80.0f);
    // Reverse depth with a 70 degree vertical field of view, as the fog checks build it.
    glm::mat4 projection(0);
    projection[1][1] = 1.0f / std::tan(glm::radians(35.0f));
    projection[0][0] = projection[1][1] * float(height) / float(width);
    projection[2][2] = .1f / (2000.f - .1f);
    projection[2][3] = -1;
    projection[3][2] = 2000.f * .1f / (2000.f - .1f);

    // A black roof over the left half of the view, high above the specks.
    constexpr std::array<BasicVertex, 4> vertices = {{
        {{{-1, -1, 0}}, {{0, 0, 1}}, {{0, 0}}},
        {{{1, -1, 0}}, {{0, 0, 1}}, {{1, 0}}},
        {{{1, 1, 0}}, {{0, 0, 1}}, {{1, 1}}},
        {{{-1, 1, 0}}, {{0, 0, 1}}, {{0, 1}}},
    }};
    constexpr std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
    auto mesh = renderer.CreateMesh({vertices, indices});
    BasicDraw roof;
    roof.mesh = &mesh;
    roof.baseColor = {0, 0, 0, 1};
    roof.model = glm::translate(glm::mat4(1), glm::vec3(-25, 6, -30)) *
                 glm::rotate(glm::mat4(1), glm::radians(-90.f), glm::vec3(1, 0, 0)) * glm::scale(glm::mat4(1), glm::vec3(25, 30, 1));

    // Two white square specks 10 m ahead: one under the roof, one in the open.
    BasicParticleDraw specks;
    specks.parameters.viewProjection = projection * lighting.view;
    specks.parameters.inverseProjection = glm::inverse(projection);
    specks.parameters.view = lighting.view;
    specks.parameters.values[0] = glm::vec4(1);
    specks.parameters.values[2].x = 1; // square: the whole quad covers its pixels
    specks.parameters.values[3] = {1, 1, 0, 0};
    specks.parameters.values[4].y = 2; // Scattering
    specks.parameters.values[28] = {0.0f, 1.0f, 0.0f, 1.0f}; // even scattering, full sun, no sky, sun shadows
    for (const float x : {-3.0f, 3.0f})
        specks.instances.push_back({glm::vec4(x, 1, -10, 0), glm::vec4(1), glm::vec4(0, 1, 0, 1.5f)});

    // The specks' centre pixels: x = 3 at 10 m projects to 0.3 * projection[0][0] in clip space.
    const int offset = static_cast<int>(std::lround(0.3f * projection[0][0] * width * 0.5f));
    const auto speck = [&](int column)
    {
        const auto pixels = readPixels(renderer.GetColorTexture());
        const std::size_t index = (static_cast<std::size_t>(height / 2) * width + column) * 4;
        require(pixels.size() > index, "Scattering readback failed");
        return static_cast<int>(pixels[index]);
    };
    const auto render = [&]
    {
        // Two frames let the shadow cascade settle as the other shadow checks do.
        for (int frame = 0; frame < 2; ++frame)
            renderer.Render(projection * lighting.view, lighting, std::span(&roof, 1), {}, {}, PostProcessDebugView::None,
                            nullptr, nullptr, true, {}, std::span(&specks, 1));
    };

    render();
    const int shaded = speck(width / 2 - offset), lit = speck(width / 2 + offset);
    std::cout << "Scattering specks: shaded " << shaded << ", lit " << lit << '\n';
    require(lit > 40, "A scattering speck in the sun must be lit (" + std::to_string(lit) + ")");
    require(shaded * 3 < lit, "The sun's shadow map must darken a scattering speck under a roof");
    specks.parameters.values[28].w = 0;
    render();
    require(std::abs(speck(width / 2 - offset) - lit) <= 6, "Sun Shadows off must light a speck under the roof");

    // Phase: the same open speck with the sun ahead of the camera (looking into it) and behind it.
    lighting.shadowsEnabled = false;
    specks.parameters.values[28] = {0.6f, 1.0f, 0.0f, 0.0f};
    lighting.directionalDirection = glm::normalize(glm::vec3(0, -0.5f, 1)); // light travels toward the camera
    render();
    const int forward = speck(width / 2 + offset);
    lighting.directionalDirection = glm::normalize(glm::vec3(0, -0.5f, -1));
    render();
    const int backward = speck(width / 2 + offset);
    std::cout << "Scattering phase: toward the sun " << forward << ", away " << backward << '\n';
    require(forward > backward * 3 + 20, "Forward scattering must make specks brightest looking toward the sun");

    renderer.Resize(previousWidth, previousHeight);
    std::cout << "Particle scattering checks passed\n";
}
