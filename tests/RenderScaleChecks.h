#pragma once
// Rendering below the output size without a temporal upscaler (dynamic resolution's native path): every post pass
// writes its own render-sized target, so the display pass stretches a complete image over the whole output. Before
// auto exposure wrote into output-sized targets, only the top-left render-sized corner was filled.
#include "PlutoGE/render/BasicRenderer.h"

#include <array>
#include <cmath>
#include <stdexcept>
#include <string>

template <class Device, class Reader>
void CheckRenderScaleOutput(PlutoGE::render::BasicRenderer &renderer, Device &, Reader readTexture)
{
    using namespace PlutoGE::render;
    const auto width = renderer.GetWidth(), height = renderer.GetHeight();
    const std::array<BasicVertex, 4> vertices{{{{{-1, -1, .5f}}, {{0, 0, 1}}, {{0, 0}}},
                                               {{{1, -1, .5f}}, {{0, 0, 1}}, {{1, 0}}},
                                               {{{1, 1, .5f}}, {{0, 0, 1}}, {{1, 1}}},
                                               {{{-1, 1, .5f}}, {{0, 0, 1}}, {{0, 1}}}}};
    const std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
    auto mesh = renderer.CreateMesh({vertices, indices});
    BasicDraw draw;
    draw.mesh = &mesh;
    draw.twoSided = true;
    draw.baseColor = {0.6f, 0.6f, 0.6f, 1.0f};
    BasicLighting lighting;
    lighting.shadowsEnabled = false;
    lighting.ambientIntensity = 0.5f;
    std::array<BasicPostProcessEffect, 2> effects{};
    effects[0].type = BasicPostProcessEffectType::AutoExposure;
    effects[1].type = BasicPostProcessEffectType::ToneMapping;
    // Half the output on each axis.
    if (!renderer.Resize(32, 32, 64, 64))
        throw std::runtime_error("Render scale check could not resize");
    renderer.Render(glm::mat4(1), lighting, std::span(&draw, 1), effects);
    const auto image = readTexture(renderer.GetColorTexture());
    if (image.size() != 64 * 64 * 4)
        throw std::runtime_error("Render scale check expected an output-sized image, got " + std::to_string(image.size()));
    const auto red = [&](int x, int y) { return int(std::to_integer<unsigned char>(image[(y * 64 + x) * 4])); };
    const int corner = red(8, 8);
    for (const auto [x, y] : {std::pair{56, 8}, std::pair{8, 56}, std::pair{56, 56}, std::pair{32, 32}})
        if (corner < 16 || std::abs(red(x, y) - corner) > 8)
            throw std::runtime_error("A render size below the output did not fill the output: " + std::to_string(corner) +
                                     " at the corner, " + std::to_string(red(x, y)) + " at " + std::to_string(x) + "," +
                                     std::to_string(y));
    renderer.Resize(width, height);
}
