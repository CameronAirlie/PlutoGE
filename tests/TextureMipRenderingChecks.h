#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>

template<class Device>
void CheckTextureMipRendering(PlutoGE::render::BasicRenderer &renderer, Device &device, bool benchmark = false)
{
    using namespace PlutoGE::render;
    const auto width = renderer.GetWidth(), height = renderer.GetHeight();
    renderer.Resize(64, 64);
    const std::array<BasicVertex, 4> vertices{{
        {{{-1,-1,.5f}}, {{0,0,1}}, {{0,0}}}, {{{1,-1,.5f}}, {{0,0,1}}, {{64,0}}},
        {{{1,1,.5f}}, {{0,0,1}}, {{64,64}}}, {{{-1,1,.5f}}, {{0,0,1}}, {{0,64}}}
    }};
    const std::array<std::uint32_t, 6> indices{0,1,2,0,2,3};
    auto mesh = renderer.CreateMesh({vertices, indices});
    BasicDraw draw; draw.mesh = &mesh; draw.twoSided = true;
    BasicLighting lighting; lighting.shadowsEnabled = false;
    for (auto format : {rhi::Format::R8G8B8A8Unorm, rhi::Format::R8G8B8A8Srgb})
        for (auto extent : {rhi::Extent2D{256,256}, rhi::Extent2D{1,256}, rhi::Extent2D{257,129}})
        {
            const bool constant = extent.width == 257;
            std::vector<std::byte> pixels(extent.width * extent.height * 4);
            for (unsigned y = 0; y < extent.height; ++y)
                for (unsigned x = 0; x < extent.width; ++x)
                {
                    const auto offset = (y * extent.width + x) * 4;
                    for (int channel = 0; channel < 3; ++channel)
                        pixels[offset + channel] = std::byte(constant ? 128 : ((x + y) & 1) * 255);
                    pixels[offset + 3] = std::byte{255};
                }
            rhi::Texture texture(device, device.CreateTexture({extent.width, extent.height, format,
                rhi::TextureUsage::Sampled, "Colour mip regression", false, 1, false, 0}, pixels));
            draw.baseColorTexture = texture.Get();
            renderer.Render(glm::mat4(1), lighting, std::span(&draw, 1), {}, {}, PostProcessDebugView::Albedo);
            const auto image = device.ReadTextureRgba8(renderer.GetColorTexture());
            const int expected = constant && format == rhi::Format::R8G8B8A8Srgb ? 55 : 128;
            for (int y = 16; y < 48; ++y)
                for (int x = 16; x < 48; ++x)
                    if (std::abs(int(std::to_integer<unsigned char>(image[(y * 64 + x) * 4])) - expected) > 4)
                        throw std::runtime_error("Colour mip filtering lost linear-light averaging or mip coverage");
        }
    if (benchmark)
    {
        std::vector<std::byte> pixels(4096ull * 4096 * 4, std::byte{128});
        const auto start = std::chrono::steady_clock::now();
        rhi::Texture texture(device, device.CreateTexture({4096, 4096, rhi::Format::R8G8B8A8Srgb,
            rhi::TextureUsage::Sampled, "4K mip upload benchmark", false, 1, false, 0}, pixels));
        std::cout << "4K sRGB texture creation: " << std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count() << " ms\n";
    }
    renderer.Resize(width, height);
}
