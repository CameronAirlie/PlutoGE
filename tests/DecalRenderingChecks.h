#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>

template<class ReadPixels>
void CheckDecalRendering(PlutoGE::render::BasicRenderer &renderer, PlutoGE::render::rhi::IRenderDevice &device, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    const auto require = [](bool ok, const char *message) { if (!ok) throw std::runtime_error(message); };
    constexpr std::array<BasicVertex, 4> vertices = {{
        {{{-.9f,-.9f,0}}, {{0,0,1}}, {{0,0}}}, {{{.9f,-.9f,0}}, {{0,0,1}}, {{1,0}}},
        {{{.9f,.9f,0}}, {{0,0,1}}, {{1,1}}}, {{{-.9f,.9f,0}}, {{0,0,1}}, {{0,1}}}
    }};
    constexpr std::array<std::uint32_t, 6> indices{0,1,2,0,2,3};
    auto mesh = renderer.CreateMesh({vertices, indices});
    BasicDraw receiver; receiver.mesh = &mesh; receiver.model[3].z = .2f;
    BasicLighting lighting; lighting.directionalIntensity = 0; lighting.ambientIntensity = .8f;
    lighting.cameraPosition = {0,0,2};
    const auto pixel = [&](const auto &pixels, float x, float y) {
        const auto offset = (std::size_t(y * renderer.GetHeight()) * renderer.GetWidth() + std::size_t(x * renderer.GetWidth())) * 4;
        return int(pixels.at(offset));
    };
    const auto render = [&](std::span<const BasicDecalDraw> decals, glm::vec2 jitter = glm::vec2(0)) {
        renderer.Render(glm::mat4(1), lighting, {&receiver, 1}, {}, {}, PostProcessDebugView::None,
            nullptr, nullptr, true, {}, {}, {}, false, jitter, decals);
        return readPixels(renderer.GetColorTexture());
    };
    const auto baseline = render({});
    require(pixel(baseline, .5f, .5f) > 20, "Decal receiver is not lit");
    BasicDecalDraw decal;
    decal.parameters.inverseModel = glm::inverse(glm::translate(glm::mat4(1), glm::vec3(0,0,.2f)) *
        glm::scale(glm::mat4(1), glm::vec3(.6f,.6f,.1f)));
    decal.parameters.color = {0,0,0,1};
    decal.parameters.projectorNormal = {0,0,1,.5f};
    const auto opaque = render({&decal, 1});
    require(pixel(opaque, .5f, .5f) < 5, "Projected decal is missing");
    require(std::abs(pixel(opaque, .8f, .5f) - pixel(baseline, .8f, .5f)) <= 2, "Decal leaked outside its projector");
    const auto centerAlpha = (std::size_t(renderer.GetHeight() / 2) * renderer.GetWidth() + renderer.GetWidth() / 2) * 4 + 3;
    require(opaque.at(centerAlpha) == baseline.at(centerAlpha), "Decal changed receiver coverage alpha");
    // A top-half offset exercises native screen origins on both APIs.
    decal.parameters.inverseModel = glm::inverse(glm::translate(glm::mat4(1), glm::vec3(0,.4f,.2f)) *
        glm::scale(glm::mat4(1), glm::vec3(.4f,.4f,.1f)));
    const auto offset = render({&decal, 1});
    require(pixel(offset, .5f, .3f) < 5 || pixel(offset, .5f, .7f) < 5, "Offset decal lost projection");
    require(std::abs(pixel(offset, .5f, .5f) - pixel(baseline, .5f, .5f)) <= 2, "Offset decal changed the center");
    const auto jittered = render({&decal, 1}, {0,.4f});
    require(pixel(jittered, .5f, .1f) < 5 || pixel(jittered, .5f, .9f) < 5, "Decal did not follow camera jitter");
    decal.parameters.inverseModel = glm::inverse(glm::translate(glm::mat4(1), glm::vec3(0,0,.2f)) *
        glm::scale(glm::mat4(1), glm::vec3(.6f,.6f,.1f)));
    decal.parameters.color.a = .5f;
    const auto faded = render({&decal, 1});
    require(pixel(faded, .5f, .5f) > 5 && pixel(faded, .5f, .5f) < pixel(baseline, .5f, .5f) - 5, "Decal lifetime fade is missing");
    decal.parameters.projectorNormal = {0,0,-1,.5f};
    const auto rejected = render({&decal, 1});
    require(std::abs(pixel(rejected, .5f, .5f) - pixel(baseline, .5f, .5f)) <= 2, "Decal projected onto a back-facing surface");
    decal.parameters.projectorNormal = {0,0,1,.5f};
    decal.parameters.inverseModel[3].z -= 10;
    const auto distant = render({&decal, 1});
    require(std::abs(pixel(distant, .5f, .5f) - pixel(baseline, .5f, .5f)) <= 2, "Decal ignored projector depth");
    const std::array<std::byte, 4> transparentPixel{std::byte{255}, std::byte{0}, std::byte{0}, std::byte{0}};
    rhi::Texture transparentTexture(device, device.CreateTexture(
        {1, 1, rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::Sampled, "Transparent decal fixture"}, transparentPixel));
    decal.parameters.inverseModel[3].z += 10;
    decal.parameters.color = {1,1,1,1};
    decal.parameters.material.z = 1;
    decal.texture = transparentTexture.Get();
    const auto transparent = render({&decal, 1});
    require(std::abs(pixel(transparent, .5f, .5f) - pixel(baseline, .5f, .5f)) <= 2, "Decal ignored texture alpha mask");
    const std::array<std::byte, 4> redPixel{std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255}};
    rhi::Texture redTexture(device, device.CreateTexture(
        {1, 1, rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::Sampled, "Opaque decal fixture"}, redPixel));
    decal.texture = redTexture.Get();
    decal.parameters.color.a = .25f;
    const auto maskedFade = render({&decal, 1});
    require(maskedFade.at(centerAlpha - 2) < baseline.at(centerAlpha - 2), "Masked decal stopped fading below its alpha cutoff");
    // Muzzle flashes use instanced billboards; verify texture alpha survives
    // that path, independently of the decal pipeline and asset parser.
    BasicParticleDraw particle;
    particle.parameters.values[0] = glm::vec4(1);
    particle.parameters.values[1].w = 1;
    particle.parameters.values[3] = {1,1,0,0};
    particle.instances.push_back({{0,0,.6f,0}, glm::vec4(1), {0,1,.5f,1}});
    const auto renderParticle = [&]() {
        renderer.Render(glm::mat4(1), lighting, {&receiver,1}, {}, {}, PostProcessDebugView::None,
            nullptr, nullptr, true, {}, {&particle,1});
        return readPixels(renderer.GetColorTexture());
    };
    particle.texture = transparentTexture.Get();
    const auto emptyFlash = renderParticle();
    require(std::abs(pixel(emptyFlash, .5f, .5f) - pixel(baseline, .5f, .5f)) <= 2, "Transparent muzzle flash became a solid quad");
    particle.texture = redTexture.Get();
    const auto coloredFlash = renderParticle();
    require(int(coloredFlash.at(centerAlpha - 2)) < 5 && pixel(coloredFlash, .5f, .5f) > 240, "Muzzle flash texture was not sampled");
    std::cout << "Decal and textured particle rendering checks passed\n";
}
