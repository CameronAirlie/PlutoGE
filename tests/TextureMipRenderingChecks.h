#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <array>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

template<class Device, class Reader>
void CheckTextureMipRendering(PlutoGE::render::BasicRenderer &renderer, Device &device, Reader readTexture, bool benchmark = false)
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
            const auto image = readTexture(renderer.GetColorTexture());
            const int expected = constant && format == rhi::Format::R8G8B8A8Srgb ? 55 : 128;
            for (int y = 16; y < 48; ++y)
                for (int x = 16; x < 48; ++x)
                    if (std::abs(int(std::to_integer<unsigned char>(image[(y * 64 + x) * 4])) - expected) > 4)
                        throw std::runtime_error("Colour mip filtering lost linear-light averaging or mip coverage");
        }
    // Supply deliberately distinct mip values: the distant quad must sample
    // the supplied lower levels, not regenerate them from the base image.
    {
        std::vector<std::byte> packed(64 * 64 * 4, std::byte{0});
        for (unsigned extent = 32; extent; extent /= 2)
            packed.resize(packed.size() + extent * extent * 4, std::byte{128});
        rhi::TextureDescriptor descriptor{64, 64, rhi::Format::R8G8B8A8Unorm,
            rhi::TextureUsage::Sampled, "Prepared normal mip regression", false, 1, false, 0, true, true};
        rhi::Texture texture(device, device.CreateTexture(descriptor, packed));
        draw.baseColorTexture = texture.Get();
        renderer.Render(glm::mat4(1), lighting, std::span(&draw, 1), {}, {}, PostProcessDebugView::Albedo);
        const auto image = readTexture(renderer.GetColorTexture());
        if (std::abs(int(std::to_integer<unsigned char>(image[(32 * 64 + 32) * 4])) - 128) > 4)
            throw std::runtime_error("Prepared normal mip levels were not uploaded");
        packed.pop_back();
        bool rejected = false;
        try { rhi::Texture invalid(device, device.CreateTexture(descriptor, packed)); }
        catch (const std::invalid_argument &) { rejected = true; }
        if (!rejected) throw std::runtime_error("Truncated normal mip chain accepted");
    }
    // Magnified textures must stay at mip zero, including pixels next to
    // interior triangle edges. Distinct coarser mips expose undefined implicit
    // gradients in branched material helpers as diagonal lines/speckles.
    {
        renderer.Resize(257, 193);
        std::vector<BasicVertex> grid;
        std::vector<std::uint32_t> triangles;
        constexpr unsigned segments = 8;
        for (unsigned y = 0; y <= segments; ++y)
            for (unsigned x = 0; x <= segments; ++x)
            {
                const float u = float(x) / segments, v = float(y) / segments;
                grid.push_back({{{u * 2 - 1, v * 2 - 1, .5f}}, {{0,0,1}}, {{u,v}}});
            }
        for (unsigned y = 0; y < segments; ++y)
            for (unsigned x = 0; x < segments; ++x)
            {
                const auto a = y * (segments + 1) + x, b = a + segments + 1;
                triangles.insert(triangles.end(), {a,a+1,b+1,a,b+1,b});
            }
        auto gridMesh = renderer.CreateMesh({grid, triangles});
        std::vector<std::byte> texels(32 * 32 * 4);
        for (unsigned y = 0; y < 32; ++y)
            for (unsigned x = 0; x < 32; ++x)
            {
                const auto offset = (y * 32 + x) * 4;
                const bool check = ((x / 4 + y / 4) & 1) != 0;
                texels[offset] = std::byte(check ? 192 : 64);
                texels[offset+1] = std::byte{128}; texels[offset+2] = std::byte{240};
                texels[offset+3] = std::byte(check ? 255 : 0);
            }
        auto packed = texels;
        for (unsigned extent = 16; extent; extent /= 2)
            packed.resize(packed.size() + extent * extent * 4, std::byte{128});
        rhi::TextureDescriptor descriptor{32, 32, rhi::Format::R8G8B8A8Unorm,
            rhi::TextureUsage::Sampled, "Triangle gradient reference", false, 1, false, 1};
        rhi::Texture baseOnly(device, device.CreateTexture(descriptor, texels));
        descriptor.debugName = "Triangle gradient distinct mips";
        descriptor.mipLevels = 0; descriptor.normalMap = descriptor.normalMipmapsProvided = true;
        rhi::Texture mipmapped(device, device.CreateTexture(descriptor, packed));
        if (readTexture(baseOnly.Get()) != readTexture(mipmapped.Get()))
            throw std::runtime_error("Triangle gradient mip-zero upload differs from base-only texture");
        BasicLighting surfaceLighting;
        surfaceLighting.ambientIntensity = .3f;
        surfaceLighting.directionalIntensity = 1;
        surfaceLighting.directionalDirection = {-.3f,-.2f,-1};
        surfaceLighting.cameraPosition = {0,0,3};
        for (int scenario = 0; scenario < 6; ++scenario)
        {
            BasicDraw surface; surface.mesh = &gridMesh; surface.twoSided = true;
            // Exercise both compact standard materials and the full graph path.
            if (scenario >= 3) surface.shaderGraphProgram = std::make_shared<ShaderGraphProgram>();
            surfaceLighting.requiredGeometryInputs = scenario >= 3
                ? BasicPostProcessInput::Normal : BasicPostProcessInput::None;
            surface.alphaMode = scenario % 3 == 2 ? 1 : 0;
            const auto renderTexture = [&](rhi::TextureHandle texture) {
                if (scenario % 3 == 1) surface.normalTexture = texture;
                else surface.baseColorTexture = texture;
                renderer.Render(glm::mat4(1), surfaceLighting, std::span(&surface, 1));
                return readTexture(renderer.GetColorTexture());
            };
            const auto reference = renderTexture(baseOnly.Get());
            const auto actual = renderTexture(mipmapped.Get());
            for (unsigned y = 8; y < 185; ++y)
                for (unsigned x = 8; x < 249; ++x)
                    for (unsigned channel = 0; channel < 3; ++channel)
                    {
                        const auto pixel = (y * 257 + x) * 4 + channel;
                        const int expected = std::to_integer<unsigned char>(reference[pixel]);
                        const int sampled = std::to_integer<unsigned char>(actual[pixel]);
                        if (std::abs(sampled - expected) > 1)
                            throw std::runtime_error("Material gradients changed magnified mip selection: scenario=" +
                                std::to_string(scenario) + " at " + std::to_string(x) + "," + std::to_string(y) +
                                " expected=" + std::to_string(expected) + " actual=" + std::to_string(sampled));
                    }
        }
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
