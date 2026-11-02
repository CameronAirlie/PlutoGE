#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

// Material ambient occlusion (BasicSurfaceTexture::Occlusion, AmbientOcclusion.slang). A grey quad faces the camera
// with a one-texel occlusion map. Under ambient light alone, occlusion 0 must remove the light, strength 0 must
// ignore the map and the channel selector must pick the authored channel. Under direct light alone, occlusion 1
// changes nothing, half occlusion keeps some head-on light (micro-shadowing) and removes grazing light.
template <class ReadPixels>
void CheckMaterialOcclusion(PlutoGE::render::BasicRenderer &renderer, PlutoGE::render::rhi::IRenderDevice &device,
                            ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    const auto require = [](bool condition, const std::string &message) {
        if (!condition) throw std::runtime_error(message);
    };
    std::array<BasicVertex, 4> vertices{};
    const std::array<std::array<float, 3>, 4> positions{{{-.8f, -.8f, .5f}, {.8f, -.8f, .5f}, {.8f, .8f, .5f}, {-.8f, .8f, .5f}}};
    for (unsigned i = 0; i < 4; ++i)
    {
        vertices[i].position = positions[i];
        vertices[i].normal = {0, 0, 1};
        vertices[i].tangent = {1, 0, 0, 1};
        vertices[i].uv = {.5f, .5f};
    }
    constexpr std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
    auto mesh = renderer.CreateMesh({vertices, indices});
    const auto texture = [&](std::uint8_t red, std::uint8_t green, const char *name)
    {
        const std::array<std::byte, 4> pixel{std::byte{red}, std::byte{green}, std::byte{0}, std::byte{255}};
        return rhi::Texture(device, device.CreateTexture({1, 1, rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::Sampled,
                                                          name, false}, pixel));
    };
    // Red holds the occlusion; green the opposite, so the wrong channel is visible.
    const auto occluded = texture(0, 255, "Occlusion 0 (red)"), half = texture(128, 0, "Occlusion 0.5 (red)"),
               open = texture(255, 0, "Occlusion 1 (red)");
    constexpr auto OcclusionIndex = static_cast<std::size_t>(BasicSurfaceTexture::Occlusion);

    BasicLighting lighting;
    lighting.cameraPosition = {0, 0, 2};
    lighting.shadowsEnabled = false;
    lighting.contactShadowLength = 0.0f;
    BasicDraw draw;
    draw.mesh = &mesh;
    draw.twoSided = true;
    draw.baseColor = {0.3f, 0.3f, 0.3f, 1.0f};
    draw.roughness = 0.8f;
    const auto sample = [&](rhi::TextureHandle occlusion, std::uint32_t channel, float strength)
    {
        draw.surfaceTextures[OcclusionIndex] = occlusion;
        draw.surfaceTextureChannels[OcclusionIndex] = channel;
        draw.occlusionStrength = strength;
        renderer.Render(glm::mat4(1), lighting, std::span(&draw, 1));
        const auto image = readPixels(renderer.GetColorTexture());
        const auto at = (renderer.GetHeight() / 2 * renderer.GetWidth() + renderer.GetWidth() / 2) * 4;
        return int(image[at + 1]);
    };

    // Ambient light only.
    lighting.ambientIntensity = 0.6f;
    lighting.directionalIntensity = 0.0f;
    const int ambient = sample({}, 0, 1.0f);
    const int ambientOccluded = sample(occluded.Get(), 0, 1.0f);
    const int ambientIgnored = sample(occluded.Get(), 0, 0.0f);
    const int ambientGreen = sample(occluded.Get(), 1, 1.0f);
    const int ambientOpen = sample(open.Get(), 0, 1.0f);
    std::cout << "Material occlusion (ambient): none " << ambient << ", occluded " << ambientOccluded << ", strength 0 "
              << ambientIgnored << ", green channel " << ambientGreen << ", open " << ambientOpen << '\n';
    require(ambient > 40, "Material occlusion scene has too little ambient light to measure");
    require(ambientOccluded * 10 < ambient, "Occlusion 0 did not remove ambient light");
    require(std::abs(ambientIgnored - ambient) <= 1, "Occlusion strength 0 did not ignore the map");
    require(std::abs(ambientGreen - ambient) <= 1, "Occlusion channel selection ignored the authored channel");
    require(std::abs(ambientOpen - ambient) <= 1, "Occlusion 1 changed ambient light");

    // Direct light only: head-on, then at 60 degrees (n.l = 0.5).
    lighting.ambientIntensity = 0.0f;
    lighting.directionalIntensity = 3.0f;
    lighting.directionalDirection = {0.0f, 0.0f, -1.0f};
    const int direct = sample({}, 0, 1.0f);
    const int directOpen = sample(open.Get(), 0, 1.0f);
    const int directHalf = sample(half.Get(), 0, 1.0f);
    lighting.directionalDirection = glm::normalize(glm::vec3(0.0f, -0.8660254f, -0.5f));
    const int grazing = sample({}, 0, 1.0f);
    const int grazingHalf = sample(half.Get(), 0, 1.0f);
    std::cout << "Material occlusion (direct): none " << direct << ", open " << directOpen << ", half " << directHalf
              << "; at 60 degrees none " << grazing << ", half " << grazingHalf << '\n';
    require(direct > 40 && grazing > 20, "Material occlusion scene has too little direct light to measure");
    require(std::abs(directOpen - direct) <= 1, "Occlusion 1 micro-shadowed direct light");
    require(directHalf < direct - 5 && directHalf * 10 > direct * 3,
            "Half occlusion did not partly micro-shadow head-on light");
    require(grazingHalf * 10 < grazing, "Half occlusion did not micro-shadow light from outside its aperture");
}
