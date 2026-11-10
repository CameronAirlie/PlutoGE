#pragma once
// Block-compressed material textures sample like their RGBA8 sources on the device (BC1 colour, BC5 normals with
// rebuilt Z), and devices reject chains that do not match their format.
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/TextureCompression.h"
#include "PlutoGE/render/rhi/FormatInfo.h"

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

template <class Device, class Reader>
void CheckCompressedTextureRendering(PlutoGE::render::BasicRenderer &renderer, Device &device, Reader readTexture)
{
    using namespace PlutoGE::render;
    if (!device.SupportsSampledFormat(rhi::Format::BC1RgbaSrgb) || !device.SupportsSampledFormat(rhi::Format::BC5RgUnorm))
    {
        std::cout << "Compressed texture rendering skipped: BC1 sRGB or BC5 unsupported\n";
        return;
    }
    const auto width = renderer.GetWidth(), height = renderer.GetHeight();
    renderer.Resize(64, 64);
    const std::array<BasicVertex, 4> vertices{{{{{-1, -1, .5f}}, {{0, 0, 1}}, {{0, 0}}},
                                               {{{1, -1, .5f}}, {{0, 0, 1}}, {{1, 0}}},
                                               {{{1, 1, .5f}}, {{0, 0, 1}}, {{1, 1}}},
                                               {{{-1, 1, .5f}}, {{0, 0, 1}}, {{0, 1}}}}};
    const std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
    auto mesh = renderer.CreateMesh({vertices, indices});
    const auto upload = [&](const std::vector<std::byte> &rgba8, rhi::Format format, TextureSemantic semantic) {
        const auto encoded = EncodeTexture(rgba8, 64, 64, semantic, format);
        if (!encoded)
            throw std::runtime_error("Compressed check could not encode its texture");
        rhi::TextureDescriptor descriptor{64, 64, format, rhi::TextureUsage::Sampled, "Compressed check"};
        descriptor.mipLevels = encoded->mipLevels;
        descriptor.mipmapsProvided = true;
        return rhi::Texture(device, device.CreateTexture(descriptor, encoded->data));
    };
    const auto fill = [](std::array<unsigned char, 4> texel) {
        std::vector<std::byte> pixels(64 * 64 * 4);
        for (std::size_t i = 0; i < pixels.size(); ++i)
            pixels[i] = std::byte(texel[i % 4]);
        return pixels;
    };

    // Colour: a BC1 sRGB albedo decodes to the same linear albedo as its RGBA8 source.
    {
        BasicDraw draw;
        draw.mesh = &mesh;
        draw.twoSided = true;
        BasicLighting lighting;
        lighting.shadowsEnabled = false;
        const auto source = fill({200, 120, 40, 255});
        rhi::Texture reference(device, device.CreateTexture({64, 64, rhi::Format::R8G8B8A8Srgb, rhi::TextureUsage::Sampled,
                                                             "Compressed check reference"}, source));
        auto compressed = upload(source, rhi::Format::BC1RgbaSrgb, TextureSemantic::Color);
        const auto render = [&](rhi::TextureHandle texture) {
            draw.baseColorTexture = texture;
            renderer.Render(glm::mat4(1), lighting, std::span(&draw, 1), {}, {}, PostProcessDebugView::Albedo);
            return readTexture(renderer.GetColorTexture());
        };
        const auto expected = render(reference.Get());
        const auto actual = render(compressed.Get());
        for (int channel = 0; channel < 3; ++channel)
        {
            const auto pixel = (32 * 64 + 32) * 4 + channel;
            if (std::abs(int(std::to_integer<unsigned char>(actual[pixel])) -
                         int(std::to_integer<unsigned char>(expected[pixel]))) > 4)
                throw std::runtime_error("BC1 sRGB albedo differs from its RGBA8 source");
        }
    }

    // Normals: a tilted BC5 normal map with Z rebuilt shades like the RGBA8 map it came from.
    {
        BasicLighting lighting;
        lighting.shadowsEnabled = false;
        // A grazing light along X: N.L is about 0.29 for the flat normal and 0.67 for the tilted one.
        lighting.ambientIntensity = 0.05f;
        lighting.directionalIntensity = 0.8f;
        lighting.directionalDirection = {-1.0f, 0.0f, -0.3f};
        lighting.cameraPosition = {0, 0, 3};
        const float nx = 0.45f, ny = -0.3f, nz = std::sqrt(1 - nx * nx - ny * ny);
        const auto encode = [](float value) { return static_cast<unsigned char>(std::lround((value * .5f + .5f) * 255)); };
        const auto source = fill({encode(nx), encode(ny), encode(nz), 255});
        rhi::TextureDescriptor normalDescriptor{64, 64, rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::Sampled,
                                                "Compressed check normal reference"};
        normalDescriptor.normalMap = true;
        rhi::Texture reference(device, device.CreateTexture(normalDescriptor, source));
        auto compressed = upload(source, rhi::Format::BC5RgUnorm, TextureSemantic::TangentNormal);
        BasicDraw draw;
        draw.mesh = &mesh;
        draw.twoSided = true;
        const auto render = [&](rhi::TextureHandle texture, NormalTextureEncoding encoding) {
            draw.normalTexture = texture;
            draw.normalEncoding = encoding;
            renderer.Render(glm::mat4(1), lighting, std::span(&draw, 1));
            return readTexture(renderer.GetColorTexture());
        };
        const auto flat = render({}, NormalTextureEncoding::Rgb);
        const auto expected = render(reference.Get(), NormalTextureEncoding::Rgb);
        const auto actual = render(compressed.Get(), NormalTextureEncoding::Xy);
        const auto mistaken = render(compressed.Get(), NormalTextureEncoding::Rgb);
        const auto at = [](const auto &image) { return int(std::to_integer<unsigned char>(image[(32 * 64 + 32) * 4])); };
        if (std::abs(at(expected) - at(flat)) < 8)
            throw std::runtime_error("Normal check is insensitive: the tilted normal did not change shading");
        if (std::abs(at(actual) - at(expected)) > 3)
            throw std::runtime_error("BC5 normals with rebuilt Z shade differently from RGBA8 normals: expected " +
                                     std::to_string(at(expected)) + ", got " + std::to_string(at(actual)));
        if (std::abs(at(mistaken) - at(expected)) <= 3)
            throw std::runtime_error("Normal check cannot tell XY normals decoded as RGB");
    }

    // A chain that does not match the format's block layout is rejected.
    {
        rhi::TextureDescriptor descriptor{64, 64, rhi::Format::BC1RgbaUnorm, rhi::TextureUsage::Sampled, "Truncated BC1"};
        descriptor.mipmapsProvided = true;
        std::vector<std::byte> truncated(rhi::TextureChainByteSize(rhi::Format::BC1RgbaUnorm, 64, 64, 7) - 8);
        bool rejected = false;
        try { rhi::Texture invalid(device, device.CreateTexture(descriptor, truncated)); }
        catch (const std::invalid_argument &) { rejected = true; }
        if (!rejected)
            throw std::runtime_error("Truncated block-compressed chain accepted");
    }
    renderer.Resize(width, height);
}
