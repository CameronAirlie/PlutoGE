#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

template<class Device, class ReadPixels>
void CheckTemporalMotionRendering(PlutoGE::render::BasicRenderer &renderer, Device &device, bool fsr, ReadPixels readPixels, unsigned frames = 48)
{
    using namespace PlutoGE::render;
    const std::array<BasicVertex, 3> vertices{{
        {{{-.5f,-.8f,.5f}}, {{0,0,1}}, {{0,0}}},
        {{{ .5f, .8f,.5f}}, {{0,0,1}}, {{1,0}}},
        {{{-.5f, .8f,.5f}}, {{0,0,1}}, {{0,1}}}
    }};
    const std::array<std::uint32_t, 3> indices{0,1,2};
    auto mesh = renderer.CreateMesh({vertices, indices});
    BasicDraw edge; edge.mesh = &mesh; edge.emission = {.8f,.8f,.8f};
    BasicDraw hidden = edge; hidden.model[3].x = 4; hidden.previousModel = hidden.model;
    BasicLighting lighting; lighting.ambientIntensity = lighting.directionalIntensity = 0;
    const auto halton = [](unsigned n, unsigned base) {
        float value = 0, fraction = 1;
        for (; n; n /= base) { fraction /= base; value += fraction * (n % base); }
        return value - .5f;
    };
    const unsigned output = fsr ? 192 : 128;
    std::vector<std::byte> reference;
    for (unsigned reordered = 0; reordered < (fsr ? 4u : 5u); ++reordered)
    {
        renderer.SetTemporalUpscalerOptions({.technology = fsr ? rhi::TemporalUpscaler::Fsr2 : rhi::TemporalUpscaler::None,
                                            .autoExposure = false, .sharpness = 0});
        renderer.Resize(127, 127, output, output);
        renderer.Resize(128, 128, output, output);
        BasicPostProcessEffect taa{BasicPostProcessEffectType::TAA};
        taa.parameters[0] = {.90f,.94f,.55f,0};
        taa.parameters[1] = {.0035f,.82f,80,1};
        taa.quality = reordered == 4 ? 0u : 1u;
        for (unsigned frame = 0; frame < frames; ++frame)
        {
            // Combined object and camera motion; the offscreen draw changes
            // list position but must never become the edge's previous model.
            const auto position = [](unsigned index) {
                const unsigned phase = index % 32;
                return (float(phase <= 16 ? phase : 32 - phase) * 2.0f - 16.0f) / 64.0f;
            };
            edge.model[3].x = reordered == 3 ? 0.0f : position(frame);
            auto previous = edge.model;
            previous[3].x = reordered == 3 ? 0.0f : position(frame ? frame - 1 : 0);
            edge.previousModel = previous;
            edge.instanceModels.reset(); edge.previousInstanceModels.reset();
            if (reordered == 2)
            {
                edge.instanceModels = std::make_shared<const std::vector<glm::mat4>>(std::vector{edge.model});
                edge.previousInstanceModels = std::make_shared<const std::vector<glm::mat4>>(std::vector{previous});
                edge.model[3].x = 4; // A surviving instance owns its transform, not the batch.
            }
            std::array draws{edge, hidden};
            if (reordered && (frame & 1)) std::swap(draws[0], draws[1]);
            glm::mat4 camera(1); camera[3].x = reordered == 3 ? 0.0f : float(frame % 48) * .125f / 64.0f;
            const glm::vec2 jitter(halton(frame % 18 + 1,2), halton(frame % 18 + 1,3));
            taa.parameters[2] = {jitter / 128.0f, 0, 0};
            rhi::TemporalUpscalerFrame upscaler;
            upscaler.contextId = 998877; upscaler.frameIndex = frame;
            upscaler.renderSize = {128,128}; upscaler.outputSize = {output,output};
            upscaler.resetHistory = frame == 0;
            upscaler.jitterPixels = {jitter.x,jitter.y};
            renderer.Render(camera, lighting, draws, std::span(&taa, fsr ? 0 : 1), {},
                PostProcessDebugView::None, fsr ? &upscaler : nullptr, &camera);
            if (fsr && !renderer.WasTemporalUpscalerEvaluated()) throw std::runtime_error("Moving FSR dispatch failed");
        }
        const auto pixels = readPixels(renderer.GetColorTexture());
        unsigned partial = 0;
        for (unsigned y = output/4; y < output*3/4; ++y)
            for (unsigned x = output/4; x < output*7/8; ++x)
            {
                const int value = int(pixels[(y*output+x)*4]);
                if (value > 20 && value < 195) ++partial;
            }
        std::cout << (fsr ? "FSR" : "TAA") << (reordered == 3 ? " stationary" : " moving") << " edge coverage pixels=" << partial << std::endl;
        if (partial < output/4) throw std::runtime_error("Temporal AA lost moving-edge coverage");
        if (!reordered) reference = pixels;
        else if (reordered < 3)
        {
            double error = 0;
            for (std::size_t at = 0; at < pixels.size(); at += 4) error += std::abs(int(pixels[at]) - int(reference[at]));
            std::cout << "Draw-order temporal error=" << error << std::endl;
            if (error > output) throw std::runtime_error("Draw order corrupted temporal object history");
        }
        if (!fsr)
        {
            // Removing the surface must reject its history, rather than
            // retaining a bright trail to obtain smooth moving silhouettes.
            for (unsigned frame = 0; frame < 3; ++frame)
                renderer.Render(glm::mat4(1), lighting, std::span(&hidden,1), std::span(&taa,1));
            const auto cleared = readPixels(renderer.GetColorTexture());
            for (std::size_t at = 0; at < cleared.size(); at += 4)
                if (int(cleared[at]) > 20) throw std::runtime_error("TAA retained a removed surface");
        }
        if (fsr) device.ReleaseTemporalUpscalerContext(998877);
    }
}
