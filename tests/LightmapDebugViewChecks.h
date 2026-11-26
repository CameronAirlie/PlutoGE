#pragma once
// The Lightmap and Lightmap Coverage debug views (docs/BAKED_LIGHTING.md): three quads side by side, one lightmapped,
// one static mesh missing from the bake (LightmapRect::staticReceiver) and one moving, read back through the display.
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/rhi/Resource.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

template <class ReadPixels>
void CheckLightmapDebugViews(PlutoGE::render::BasicRenderer &renderer, PlutoGE::render::rhi::IRenderDevice &device,
                             ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    constexpr int width = 48, height = 16;
    const auto require = [](bool condition, const std::string &message) {
        if (!condition) throw std::runtime_error("Lightmap debug views: " + message);
    };
    // Later checks share this renderer and some rely on the size the checks before them left: restore it on exit.
    struct RestoreSize
    {
        BasicRenderer &renderer;
        int width, height;
        ~RestoreSize() { renderer.Resize(width, height); }
    } restoreSize{renderer, int(renderer.GetWidth()), int(renderer.GetHeight())};
    require(renderer.Resize(width, height), "resize failed");
    std::array<BasicVertex, 4> vertices{};
    const std::array<glm::vec2, 4> corners{{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}};
    for (std::size_t index = 0; index < vertices.size(); ++index)
    {
        vertices[index].position = {corners[index].x, corners[index].y, 0.0f};
        vertices[index].normal = {0.0f, 0.0f, 1.0f};
        vertices[index].uv = {corners[index].x * 0.5f + 0.5f, corners[index].y * 0.5f + 0.5f};
        vertices[index].uv2 = vertices[index].uv;
    }
    const std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
    auto mesh = renderer.CreateMesh({vertices, indices});

    // A uniform atlas: every texel holds mean radiance (0.6, 0.3, 0.1) and no static shadow.
    std::vector<std::uint16_t> texels;
    for (int texel = 0; texel < 16; ++texel)
        for (const float value : {0.6f, 0.3f, 0.1f, 1.0f})
            texels.push_back(glm::packHalf1x16(value));
    rhi::TextureDescriptor descriptor;
    descriptor.width = 4;
    descriptor.height = 4;
    descriptor.format = rhi::Format::R16G16B16A16Float;
    descriptor.mipLevels = 1;
    descriptor.debugName = "Lightmap debug view atlas";
    rhi::Texture atlas(device, device.CreateTexture(descriptor, std::as_bytes(std::span(texels))));

    std::array<BasicDraw, 3> draws{};
    for (std::size_t index = 0; index < draws.size(); ++index)
    {
        draws[index].mesh = &mesh;
        draws[index].castsShadow = false;
        draws[index].model = glm::translate(glm::mat4(1.0f), glm::vec3(-0.66f + 0.66f * float(index), 0.0f, 0.5f)) *
                             glm::scale(glm::mat4(1.0f), glm::vec3(0.3f, 0.9f, 1.0f));
    }
    draws[0].lightmap.scale = {1.0f, 1.0f};
    draws[0].lightmap.staticReceiver = true;
    draws[1].lightmap.staticReceiver = true; // static, but the bake left it out

    BasicLighting lighting;
    lighting.shadowsEnabled = false;
    lighting.bakedLighting.active = true;
    lighting.bakedLighting.atlas = atlas.Get();
    lighting.bakedLighting.atlasSize = {4, 4};

    struct Pixel { int r, g, b; };
    const auto render = [&](PostProcessDebugView view) {
        renderer.Render(glm::mat4(1.0f), lighting, draws, {}, {}, view);
        const auto pixels = readPixels(renderer.GetColorTexture());
        require(pixels.size() == std::size_t(width) * height * 4, "readback size");
        std::array<Pixel, 3> centres{};
        for (int index = 0; index < 3; ++index)
        {
            const auto offset = (std::size_t(height / 2) * width + std::size_t(8 + 16 * index)) * 4;
            centres[index] = {int(pixels[offset]), int(pixels[offset + 1]), int(pixels[offset + 2])};
        }
        return centres;
    };
    const auto describe = [](const Pixel &pixel) {
        return "(" + std::to_string(pixel.r) + ", " + std::to_string(pixel.g) + ", " + std::to_string(pixel.b) + ")";
    };

    auto coverage = render(PostProcessDebugView::LightmapCoverage);
    require(coverage[0].g > coverage[0].r + 50 && coverage[0].g > coverage[0].b + 50,
            "a lightmapped surface is not green: " + describe(coverage[0]));
    require(coverage[1].r > coverage[1].g + 60 && coverage[1].r > coverage[1].b + 60,
            "a static mesh missing from the bake is not red: " + describe(coverage[1]));
    require(coverage[2].r > 40 && std::abs(coverage[2].r - coverage[2].g) < 25 && std::abs(coverage[2].g - coverage[2].b) < 25,
            "a moving surface is not grey: " + describe(coverage[2]));

    auto lightmap = render(PostProcessDebugView::Lightmap);
    require(lightmap[0].r > lightmap[0].g + 20 && lightmap[0].g > lightmap[0].b + 20 && lightmap[0].r > 60,
            "the lightmap view does not show the baked radiance: " + describe(lightmap[0]));
    for (int index = 1; index < 3; ++index)
        require(lightmap[index].r * 2 < lightmap[0].r && std::abs(lightmap[index].r - lightmap[index].b) < 15,
                "a surface without lightmap texels is not dark grey: " + describe(lightmap[index]));

    // The scene's lighting is Realtime: the lightmapped draw keeps its rect but samples nothing.
    lighting.bakedLighting = {};
    coverage = render(PostProcessDebugView::LightmapCoverage);
    require(coverage[0].r > 120 && coverage[0].g > 100 && coverage[0].b + 60 < coverage[0].g,
            "a lightmapped surface with baked lighting off is not yellow: " + describe(coverage[0]));
    require(coverage[1].r > coverage[1].g + 60, "the missing static mesh lost its mark with baked lighting off");

    // Ordinary frames are untouched by the views.
    renderer.Render(glm::mat4(1.0f), lighting, draws);
    std::cout << "PASS lightmap debug views\n";
}
