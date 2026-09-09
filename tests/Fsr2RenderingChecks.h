#pragma once

#include "PlutoGE/render/BasicRenderer.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

template<class Device>
void CheckFsr2Rendering(PlutoGE::render::BasicRenderer &renderer, Device &device)
{
    using namespace PlutoGE::render;
    if (!device.GetTemporalUpscalerSupport(rhi::TemporalUpscaler::Fsr2).supported)
        throw std::runtime_error("FSR2 unavailable for accumulation regression");
    const auto halton = [](unsigned index, unsigned base) {
        float value = 0, fraction = 1;
        for (; index; index /= base) { fraction /= base; value += fraction * (index % base); }
        return value - 0.5f;
    };
    renderer.SetTemporalUpscalerOptions({.technology = rhi::TemporalUpscaler::Fsr2,
                                       .autoExposure = false, .sharpness = 0});
    renderer.Resize(128, 128, 192, 192);
    const std::array<BasicVertex,4> vertices{{
        {{{-1,-1,.5f}}, {{0,0,1}}, {{0,0}}}, {{{1,-1,.5f}}, {{0,0,1}}, {{1,0}}},
        {{{1,1,.5f}}, {{0,0,1}}, {{1,1}}}, {{{-1,1,.5f}}, {{0,0,1}}, {{0,1}}}
    }};
    const std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    // A continuous receiver prevents disocclusion rejection from hiding the
    // unwanted accumulation of jittered material detail in GI history.
    std::vector<std::byte> texels(512*512*4);
    for (unsigned y = 0; y < 512; ++y)
        for (unsigned x = 0; x < 512; ++x)
        {
            const auto value = std::byte(((x + y/4) / 8) % 2 ? 220 : 30);
            const auto at = (y*512+x)*4;
            texels[at] = texels[at+1] = texels[at+2] = value;
            texels[at+3] = std::byte{255};
        }
    rhi::Texture texture(device, device.CreateTexture({512,512,rhi::Format::R8G8B8A8Unorm,
        rhi::TextureUsage::Sampled,"FSR2 stationary material detail"}, texels));
    auto mesh = renderer.CreateMesh({vertices, indices});
    BasicDraw draw; draw.mesh = &mesh; draw.twoSided = true;
    draw.baseColorTexture = texture.Get();
    BasicLighting lighting; lighting.shadowsEnabled = false;
    lighting.ambientIntensity = 1; lighting.directionalIntensity = 0;
    BasicPostProcessEffect effect{BasicPostProcessEffectType::VCTGI};
    effect.historyOwner = &effect;
    effect.quality = 1;
    // Zero GI energy must leave scene detail intact, even with GI history on.
    effect.parameters[0] = {40,0,1.2f,20};
    effect.parameters[1] = {.35f,.92f,.25f,.9f};
    effect.parameters[2] = {64,1,0,1};
    effect.parameters[3] = {0,0,0,8};
    const glm::mat4 projection(1);
    std::array<double,2> detail{};
    for (unsigned withGi = 0; withGi < 2; ++withGi)
    {
    for (unsigned index = 0; index < 180; ++index)
    {
        rhi::TemporalUpscalerFrame frame;
        frame.renderSize = {128,128}; frame.outputSize = {192,192};
        frame.contextId = 987654; frame.frameIndex = index;
        frame.resetHistory = index == 0;
        frame.jitterPixels = {halton(index % 18 + 1, 2), halton(index % 18 + 1, 3)};
        renderer.Render(projection, lighting, std::span(&draw,1), std::span(&effect,withGi), {},
                        PostProcessDebugView::None, &frame, &projection);
        if (!renderer.WasTemporalUpscalerEvaluated()) throw std::runtime_error("FSR2 dispatch failed");
        const auto pixels = device.ReadTextureRgba8(renderer.GetColorTexture());
        double contrast = 0;
        for (unsigned y = 48; y < 144; ++y)
            for (unsigned x = 24; x < 168; ++x)
                contrast += std::abs(int(std::to_integer<unsigned char>(pixels[(y*192+x)*4])) -
                                     int(std::to_integer<unsigned char>(pixels[(y*192+x-1)*4])));
        if (index >= 162) detail[withGi] += contrast;
    }
    }
    device.ReleaseTemporalUpscalerContext(987654);
    std::cout << "FSR2 stationary detail retained with GI history: "
              << detail[1] / std::max(detail[0], 1.0) << '\n';
    if (detail[0] <= 0 || detail[1] < detail[0] * .98)
        throw std::runtime_error("GI history blurred stationary FSR2 material detail");
}
