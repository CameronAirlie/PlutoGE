#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <array>
#include <cstdlib>
#include <stdexcept>
#include <vector>

// Terrain layers on the GPU: a weight map giving the quad's left half to a red layer and its right half to a blue
// one must shade each half with its layer's albedo array slice, with linear and height blending, through the RHI
// texture arrays (both backends) and the BasicLitTerrain variant.
template<class ReadPixels>
void CheckTerrainLayers(PlutoGE::render::BasicRenderer &renderer, PlutoGE::render::rhi::IRenderDevice &device,
                        ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    std::array<BasicVertex, 4> vertices{};
    const std::array<std::array<float, 3>, 4> positions{{{-.8f, -.8f, .5f}, {.8f, -.8f, .5f}, {.8f, .8f, .5f}, {-.8f, .8f, .5f}}};
    const std::array<std::array<float, 2>, 4> uvs{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
    for (unsigned i = 0; i < 4; ++i)
    {
        vertices[i].position = positions[i];
        vertices[i].normal = {0, 0, 1};
        vertices[i].uv = uvs[i];
    }
    constexpr std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
    auto mesh = renderer.CreateMesh({vertices, indices});
    const auto bytes = [](std::initializer_list<int> values) {
        std::vector<std::byte> data;
        for (const int value : values) data.push_back(std::byte(value));
        return data;
    };
    // Weight map 2x1: left texel all layer 0 (R), right texel all layer 1 (G).
    rhi::Texture weights(device, device.CreateTexture({2, 1, rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::Sampled,
        "Terrain weights", false, 1, false, 1}, bytes({255, 0, 0, 0, 0, 255, 0, 0})));
    const auto array = [&](rhi::Format format, std::initializer_list<int> data, const char *name) {
        rhi::TextureDescriptor descriptor{1, 1, format, rhi::TextureUsage::Sampled, name};
        descriptor.arrayLayers = 2;
        return rhi::Texture(device, device.CreateTexture(descriptor, bytes(data)));
    };
    // Albedo alpha is height: layer 1 is the taller surface, so height blending favours it. Normal alpha is roughness.
    auto albedo = array(rhi::Format::R8G8B8A8Srgb, {255, 0, 0, 0, 0, 0, 255, 255}, "Terrain albedo array");
    auto normal = array(rhi::Format::R8G8B8A8Unorm, {128, 128, 255, 255, 128, 128, 255, 255}, "Terrain normal array");

    auto terrain = std::make_shared<BasicTerrainLayers>();
    terrain->weights[0] = weights.Get();
    terrain->albedo = albedo.Get();
    terrain->normal = normal.Get();
    terrain->layerCount = 2;
    terrain->tiling = {glm::vec4(1, 1, 1, 0), glm::vec4(1, 1, 1, 0)};
    terrain->tint = {glm::vec4(1), glm::vec4(1)};

    BasicLighting lighting;
    lighting.cameraPosition = {0, 0, 2};
    BasicDraw draw;
    draw.mesh = &mesh;
    draw.twoSided = true;
    draw.terrainLayers = terrain;
    const auto require = [](bool condition, const char *message) { if (!condition) throw std::runtime_error(message); };
    const auto sample = [&](float x) {
        renderer.Render(glm::mat4(1), lighting, std::span(&draw, 1));
        const auto image = readPixels(renderer.GetColorTexture());
        const auto at = (renderer.GetHeight() / 2 * renderer.GetWidth() + static_cast<unsigned>(renderer.GetWidth() * x)) * 4;
        return std::array<int, 3>{int(image[at]), int(image[at + 1]), int(image[at + 2])};
    };
    const auto red = [](const std::array<int, 3> &c) { return c[0] > 20 && c[0] > 4 * (c[2] + 1); };
    const auto blue = [](const std::array<int, 3> &c) { return c[2] > 20 && c[2] > 4 * (c[0] + 1); };

    require(red(sample(0.3f)), "Terrain layer 0 did not shade the quad's left half from the albedo array");
    require(blue(sample(0.7f)), "Terrain layer 1 did not shade the quad's right half from the albedo array");
    terrain->heightBlend = 0.25f;
    require(red(sample(0.3f)) && blue(sample(0.7f)), "Height blending broke fully painted texels");
    // At the seam both layers have weight; the taller layer 1 wins under height blending.
    const auto seam = sample(0.5f);
    require(seam[2] > seam[0], "Height blending did not favour the taller layer at the seam");
    terrain->heightBlend = 0.0f;

    // Past the detail distance the draw shows its material albedo (white here) instead of the layers; inside the
    // fade band it is part way between. The camera is about 1.5 units from the quad.
    terrain->detailDistance = 0.5f;
    terrain->detailFade = 0.25f;
    const auto far = sample(0.3f);
    require(far[1] > 20 && std::abs(far[0] - far[1]) < 8 && std::abs(far[2] - far[1]) < 8,
            "Terrain past the detail distance still showed its layers");
    terrain->detailDistance = 1.0f;
    terrain->detailFade = 2.0f;
    const auto fading = sample(0.3f);
    require(fading[0] > fading[1] + 20 && fading[1] > 10, "Terrain inside the detail fade band did not blend layers and albedo");
    terrain->detailDistance = terrain->detailFade = 0.0f;

    // Region updates patch textures in place (IRenderDevice::UpdateTexture): the left weight texel now belongs to
    // layer 1, then layer 1's albedo (array layer 1) turns green.
    require(device.UpdateTexture(weights.Get(), {0, 0, 1, 1}, bytes({0, 255, 0, 0})), "Weight map region update failed");
    require(blue(sample(0.3f)), "A weight map region update did not reach the GPU");
    require(device.UpdateTexture(albedo.Get(), {0, 0, 1, 1, 1}, bytes({0, 255, 0, 255})), "Array layer region update failed");
    const auto green = sample(0.3f);
    require(green[1] > 20 && green[1] > 4 * (green[0] + green[2] + 1), "An array layer region update did not reach the GPU");
    require(!device.UpdateTexture(weights.Get(), {1, 0, 2, 1}, bytes({0, 0, 0, 0, 0, 0, 0, 0})),
            "A region outside the texture was accepted");

    // Without the terrain shaders a layered draw still renders (its plain material), never a broken pipeline.
    auto fallback = draw;
    fallback.terrainLayers.reset();
    renderer.Render(glm::mat4(1), lighting, std::span(&fallback, 1));
}
