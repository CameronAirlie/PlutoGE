#pragma once

#include "PlutoGE/render/BasicRenderer.h"
#include <array>
#include <algorithm>
#include <stdexcept>
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>

// Magnify a deliberately coarse shadow map. A nearest-comparison 3x3 PCF
// can produce only ten levels; interpolated coverage must preserve a smooth
// edge even without temporal AA or a screen-space blur.
template <class ReadPixels>
void CheckShadowFiltering(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    const auto width = renderer.GetWidth();
    const auto height = renderer.GetHeight();
    if (!renderer.Resize(256, 256))
        throw std::runtime_error("Shadow filter test resize failed");
    constexpr std::array<BasicVertex, 4> vertices = {{
        {{{-.9f,-.9f,0}}, {{0,0,1}}, {{0,0}}},
        {{{ .9f,-.9f,0}}, {{0,0,1}}, {{1,0}}},
        {{{ .9f, .9f,0}}, {{0,0,1}}, {{1,1}}},
        {{{-.9f, .9f,0}}, {{0,0,1}}, {{0,1}}}
    }};
    constexpr std::array<std::uint32_t, 6> indices{0,1,2,0,2,3};
    auto mesh = renderer.CreateMesh({vertices, indices});
    BasicDraw receiver;
    receiver.mesh = &mesh;
    receiver.twoSided = true;
    receiver.model[3].z = .8f;
    BasicDraw caster = receiver;
    caster.model = glm::translate(glm::mat4(1), glm::vec3(-.45f, 0, .2f)) *
                   glm::rotate(glm::mat4(1), .25f, glm::vec3(0,0,1)) *
                   glm::scale(glm::mat4(1), glm::vec3(.5f, 2, 1));
    BasicLighting lighting;
    lighting.shadowsEnabled = true;
    lighting.shadowCascadeCount = 1;
    lighting.shadowResolution = 256;
    lighting.shadowFilterEnabled = false;
    lighting.shadowSoftness = 1;
    lighting.shadowMatrices[0] = glm::scale(glm::mat4(1), glm::vec3(.1f,.1f,1));
    lighting.directionalDirection = {0,0,-1};
    renderer.Render(glm::mat4(1), lighting, std::span(&receiver, 1), {},
                    std::span(&caster, 1), PostProcessDebugView::DirectionalShadowMaskFiltered);
    const auto pixels = readPixels(renderer.GetColorTexture());
    if (pixels.size() != 256 * 256 * 4)
        throw std::runtime_error("Shadow filter readback size mismatch");
    std::array<bool, 256> levels{};
    // Ignore the receiver boundary and background.
    for (int y = 32; y < 224; ++y)
        for (int x = 32; x < 224; ++x)
            levels[static_cast<unsigned char>(pixels[(y * 256 + x) * 4])] = true;
    const auto intermediateLevels = std::count(levels.begin() + 1, levels.end() - 1, true);
    // The scene's screen-space blur radius must not change the RHI shadow
    // footprint. It previously overrode softness and spread taps four texels
    // apart even with shadow softness set to one.
    lighting.shadowFilterEnabled = true;
    lighting.shadowFilterRadius = 4;
    renderer.Render(glm::mat4(1), lighting, std::span(&receiver, 1), {},
                    std::span(&caster, 1), PostProcessDebugView::DirectionalShadowMaskFiltered);
    if (readPixels(renderer.GetColorTexture()) != pixels)
        throw std::runtime_error("Screen-space filter radius changed shadow-map coverage");
    lighting.shadowFilterEnabled = false;

    // A straight, magnified edge exposes plateaus hidden by counting unique
    // levels across a diagonal. Sparse bilinear taps at radius two create
    // several-pixel constant bands at intermediate visibility.
    caster.model = glm::translate(glm::mat4(1), glm::vec3(-.45f, 0, .2f)) *
                   glm::scale(glm::mat4(1), glm::vec3(.5f, 2, 1));
    lighting.shadowSoftness = 2;
    renderer.Render(glm::mat4(1), lighting, std::span(&receiver, 1), {},
                    std::span(&caster, 1), PostProcessDebugView::DirectionalShadowMaskFiltered);
    const auto edge = readPixels(renderer.GetColorTexture());
    if (edge.size() != 256 * 256 * 4)
        throw std::runtime_error("Shadow edge readback size mismatch");
    int longestPlateau = 0;
    int plateau = 0;
    int previous = -1;
    for (int x = 32; x < 224; ++x)
    {
        const int value = static_cast<unsigned char>(edge[(128 * 256 + x) * 4]);
        plateau = value > 20 && value < 235 && value == previous ? plateau + 1 : 1;
        longestPlateau = std::max(longestPlateau, plateau);
        previous = value;
    }
    std::cout << "Shadow edge: longest intermediate plateau " << longestPlateau << " pixels\n";
    if (longestPlateau > 2)
        throw std::runtime_error("Shadow filtering contains sparse-sample coverage plateaus");
    if (!renderer.Resize(width, height))
        throw std::runtime_error("Shadow filter test restore failed");
    std::cout << "Shadow filter: " << intermediateLevels << " intermediate coverage levels\n";
    if (!levels[0] || !levels[255] || intermediateLevels < 16)
        throw std::runtime_error("Shadow edge lacks smooth sub-texel comparison coverage");
}
