#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <array>
#include <cmath>
#include <stdexcept>

// Model-style BLEND meshes often contain overlapping solid surfaces in one
// primitive. Object sorting cannot order these triangles correctly.
template <class ReadPixels>
void CheckTransparencyDepth(PlutoGE::render::BasicRenderer &renderer,
                            PlutoGE::render::rhi::IRenderDevice &device, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    const auto require = [](bool condition, const char *message) {
        if (!condition) throw std::runtime_error(message);
    };
    // Deliberately submit the green front surface before the red rear surface.
    constexpr std::array<BasicVertex, 8> vertices = {{
        {{{-.8f,-.8f,.7f}}, {{0,0,1}}, {{.75f,.5f}}},
        {{{ .8f,-.8f,.7f}}, {{0,0,1}}, {{.75f,.5f}}},
        {{{ .8f, .8f,.7f}}, {{0,0,1}}, {{.75f,.5f}}},
        {{{-.8f, .8f,.7f}}, {{0,0,1}}, {{.75f,.5f}}},
        {{{-.8f,-.8f,.3f}}, {{0,0,1}}, {{.25f,.5f}}},
        {{{ .8f,-.8f,.3f}}, {{0,0,1}}, {{.25f,.5f}}},
        {{{ .8f, .8f,.3f}}, {{0,0,1}}, {{.25f,.5f}}},
        {{{-.8f, .8f,.3f}}, {{0,0,1}}, {{.25f,.5f}}}
    }};
    constexpr std::array<std::uint32_t, 12> indices{0,1,2,0,2,3,4,5,6,4,6,7};
    auto mesh = renderer.CreateMesh({vertices, indices});
    BasicLighting lighting;
    lighting.cameraPosition = {0,0,2};
    lighting.ambientIntensity = lighting.directionalIntensity = 0;
    BasicDraw draw;
    draw.mesh = &mesh;
    draw.emission = {1,1,1};
    draw.twoSided = true;
    const auto render = [&](std::span<const BasicDraw> draws) {
        renderer.Render(glm::mat4(1), lighting, draws);
        const auto pixels = readPixels(renderer.GetColorTexture());
        const auto center = (renderer.GetHeight()/2 * renderer.GetWidth() + renderer.GetWidth()/2) * 4;
        return std::array<int,3>{int(pixels[center]), int(pixels[center+1]), int(pixels[center+2])};
    };
    for (unsigned char alpha : {255, 253, 252, 128, 0})
    {
        const std::array<std::byte, 8> pixels{std::byte{255},std::byte{0},std::byte{0},std::byte{255},
            std::byte{0},std::byte{255},std::byte{0},std::byte{alpha}};
        rhi::Texture texture(device, device.CreateTexture({2, 1, rhi::Format::R8G8B8A8Unorm,
            rhi::TextureUsage::Sampled, "Mixed BLEND coverage", false}, pixels));
        draw.baseColorTexture = texture.Get();
        draw.alphaMode = 0;
        const auto opaque = render(std::span(&draw,1));
        draw.alphaMode = 2;
        const auto blended = render(std::span(&draw,1));
        if (alpha >= 253)
        {
            require(opaque[1] > 200 && opaque[0] < 5, "Opaque depth reference is invalid");
            require(blended == opaque, "Solid BLEND triangles overwrite nearer surfaces in the same mesh");
            auto rear = draw;
            rear.baseColor = {1,0,0,1};
            rear.emission = {1,0,0};
            rear.model[3].z = -.1f;
            // Deliberately wrong sort bounds must not affect opaque coverage.
            rear.shadowBoundsRadius = 1;
            rear.shadowBoundsCenter = {0,0,10};
            const std::array draws{draw, rear};
            require(render(draws) == opaque, "Solid BLEND depth depends on object sorting");
            auto instances = std::make_shared<std::vector<glm::mat4>>(2, glm::mat4(1));
            instances->back()[3].z = -.1f;
            draw.instanceModels = instances;
            require(render(std::span(&draw,1)) == opaque, "Solid BLEND instances lost depth ordering");
            draw.instanceModels.reset();
            draw.twoSided = false;
            require(render(std::span(&draw,1)) == opaque, "Solid BLEND front face was culled");
            draw.model[0].x = -1;
            const auto backface = render(std::span(&draw,1));
            require(backface != opaque, "Single-sided BLEND back face wrote opaque coverage");
            draw.twoSided = true;
            require(render(std::span(&draw,1)) == opaque, "Two-sided BLEND back face was culled");
            draw.model = glm::mat4(1);
        }
        else if (alpha != 0)
            require(std::abs(blended[0] - (255 - int(alpha))) <= 2 &&
                    std::abs(blended[1] - int(alpha)) <= 2,
                "Partial BLEND coverage hid its opaque background or was composited twice");
        else
            require(blended[0] > 200 && blended[1] < 5, "Zero BLEND coverage occluded its background");
    }
}
