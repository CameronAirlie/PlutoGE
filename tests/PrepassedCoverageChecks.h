#pragma once
// Prepassed alpha-tested surfaces shade with an equal depth test and no discard (kSurfacePrepassedCoverage): the image
// must equal the discarding path, including holes in near cards that show cards and ground behind them.
#include "PlutoGE/render/BasicRenderer.h"

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

template <class Device, class ReadPixels>
void CheckPrepassedCoverage(PlutoGE::render::BasicRenderer &renderer, Device &device, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    const auto width = renderer.GetWidth(), height = renderer.GetHeight();
    if (!renderer.Resize(128, 96))
        throw std::runtime_error("Prepassed coverage fixture resize failed");
    // A checker of opaque and fully transparent texels.
    std::vector<std::byte> texels(32 * 32 * 4);
    for (unsigned y = 0; y < 32; ++y)
        for (unsigned x = 0; x < 32; ++x)
        {
            const bool solid = ((x / 4 + y / 4) & 1) != 0;
            const auto offset = (y * 32 + x) * 4;
            texels[offset] = std::byte(solid ? 200 : 30);
            texels[offset + 1] = std::byte(solid ? 160 : 30);
            texels[offset + 2] = std::byte(90);
            texels[offset + 3] = std::byte(solid ? 255 : 0);
        }
    rhi::Texture checker(device, device.CreateTexture({32, 32, rhi::Format::R8G8B8A8Srgb, rhi::TextureUsage::Sampled,
                                                       "Prepassed coverage checker"}, texels));
    const std::array<BasicVertex, 4> quad{{{{{-1, -1, 0}}, {{0, 0, 1}}, {{0, 0}}}, {{{1, -1, 0}}, {{0, 0, 1}}, {{1, 0}}},
                                           {{{1, 1, 0}}, {{0, 0, 1}}, {{1, 1}}}, {{{-1, 1, 0}}, {{0, 0, 1}}, {{0, 1}}}}};
    const std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
    auto mesh = renderer.CreateMesh({quad, indices});
    // Three staggered masked cards in front of an opaque ground card.
    std::vector<BasicDraw> draws(4);
    for (std::size_t index = 0; index < draws.size(); ++index)
    {
        auto &draw = draws[index];
        draw.mesh = &mesh;
        draw.twoSided = true;
        const bool ground = index == 3;
        draw.model = glm::translate(glm::mat4(1), glm::vec3(ground ? 0.0f : 0.3f * float(index) - 0.3f,
                                                              ground ? 0.0f : 0.2f * float(index) - 0.2f,
                                                              ground ? -6.0f : -2.0f - float(index)));
        draw.baseColor = ground ? glm::vec4(0.2f, 0.5f, 0.8f, 1.0f) : glm::vec4(1.0f);
        if (!ground)
        {
            draw.baseColorTexture = checker.Get();
            draw.alphaMode = 1;
            draw.alphaCutoff = 0.5f;
        }
        else
            draw.model = glm::scale(draw.model, glm::vec3(4.0f));
    }
    BasicLighting lighting;
    lighting.shadowsEnabled = false;
    lighting.ambientIntensity = 0.6f;
    const glm::mat4 viewProjection = glm::perspectiveRH_ZO(glm::radians(60.0f), 128.0f / 96.0f, 0.1f, 50.0f);
    const auto render = [&](bool prepassedCoverage) {
        renderer.SetPrepassedCoverage(prepassedCoverage);
        renderer.Render(viewProjection, lighting, draws);
        return readPixels(renderer.GetColorTexture());
    };
    const auto discarding = render(false);
    const auto prepassed = render(true);
    renderer.SetPrepassedCoverage(true);
    if (discarding.size() != prepassed.size())
        throw std::runtime_error("Prepassed coverage images differ in size");
    std::size_t differing = 0;
    for (std::size_t i = 0; i < discarding.size(); i += 4)
        for (std::size_t channel = 0; channel < 3; ++channel)
            if (std::abs(std::to_integer<int>(discarding[i + channel]) - std::to_integer<int>(prepassed[i + channel])) > 2)
            {
                ++differing;
                break;
            }
    if (differing != 0)
        throw std::runtime_error("Prepassed coverage changed " + std::to_string(differing) + " pixels of masked cards");
    renderer.Resize(width, height);
}
