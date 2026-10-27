#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include "TemporalReconstructionPasses.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>
#include <span>
#include <vector>

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

// Background with no surface must carry camera motion in BasicLit's
// convention. Under a pure camera rotation a static point's motion depends
// only on its view direction, so pixels either side of a surface silhouette
// must agree; a background left at zero motion (or a sign error) cannot.
template<class ReadColor, class ReadFloat>
void CheckBackgroundMotion(PlutoGE::render::BasicRenderer &renderer, ReadColor readColor, ReadFloat readFloat)
{
    using namespace PlutoGE::render;
    constexpr unsigned size = 96;
    renderer.SetTemporalUpscalerOptions({});
    renderer.Resize(size, size);
    // A bright wall covering the left half of the view.
    const std::array<BasicVertex, 4> vertices{{
        {{{-40.f,-40.f,-5.f}}, {{0,0,1}}, {{0,0}}}, {{{0.f,-40.f,-5.f}}, {{0,0,1}}, {{1,0}}},
        {{{0.f,40.f,-5.f}}, {{0,0,1}}, {{1,1}}}, {{{-40.f,40.f,-5.f}}, {{0,0,1}}, {{0,1}}}}};
    const std::array<std::uint32_t, 6> indices{0,1,2,0,2,3};
    auto mesh = renderer.CreateMesh({vertices, indices});
    BasicDraw wall; wall.mesh = &mesh; wall.emission = {1.f,1.f,1.f}; wall.castsShadow = false;
    BasicLighting lighting; lighting.ambientIntensity = lighting.directionalIntensity = 0; lighting.shadowsEnabled = false;
    BasicPostProcessEffect taa{BasicPostProcessEffectType::TAA};
    taa.parameters[0] = {.90f,.94f,.55f,0};
    taa.parameters[1] = {.0035f,.82f,80,1};
    const auto projection = glm::perspectiveRH_ZO(glm::radians(60.f), 1.f, 100.f, .1f);
    for (int frame = 0; frame < 2; ++frame)
    {
        lighting.view = glm::rotate(glm::mat4(1.f), glm::radians(3.f * frame), glm::vec3(0.f, 1.f, 0.f));
        renderer.Render(projection * lighting.view, lighting, std::span(&wall, 1), std::span(&taa, 1));
    }
    const auto color = readColor(renderer.GetColorTexture());
    const auto motion = readFloat(renderer.GetMotionTexture());
    if (color.size() != size * size * 4 || motion.size() != size * size * 4)
        throw std::runtime_error("Background motion readback failed");
    const unsigned row = size / 2;
    unsigned edge = 0;
    for (unsigned x = 1; x < size && !edge; ++x)
        if ((int(color[(row * size + x - 1) * 4]) > 127) != (int(color[(row * size + x) * 4]) > 127)) edge = x;
    if (edge < 4 || edge + 4 > size) throw std::runtime_error("Background motion test silhouette not found");
    const auto at = [&](unsigned x) { return glm::vec2(motion[(row * size + x) * 4], motion[(row * size + x) * 4 + 1]); };
    const glm::vec2 surface = at(edge - 3), background = at(edge + 2);
    std::cout << "Background motion: surface " << surface.x << "," << surface.y
              << " background " << background.x << "," << background.y << std::endl;
    if (std::abs(background.x) < 0.01f) throw std::runtime_error("Background kept zero motion under camera rotation");
    if (glm::length(surface - background) > 0.003f)
        throw std::runtime_error("Background motion disagrees with surface motion across a silhouette");
}

// Upscaler sharpening of HDR colour must stay inside the local range: a
// bright edge over black may not ring above its radiance or below zero (the
// failure of an LDR sharpener on linear HDR), while a soft ramp still sharpens.
template<class ReadFloat>
void CheckUpscalerSharpening(PlutoGE::render::rhi::IRenderDevice &device,
                             const PlutoGE::render::BasicRendererShaderPackage &shaders, ReadFloat readFloat)
{
    using namespace PlutoGE::render;
    UpscalerSharpening sharpening;
    if (!sharpening.Initialize(device, {shaders.upscalerSharpening.vertex, shaders.upscalerSharpening.fragment}))
        throw std::runtime_error("Upscaler sharpening unavailable");
    constexpr std::uint32_t size = 32;
    constexpr float radiance = 40.f;
    // Columns: a hard HDR edge at x = 8 and a soft ramp from x = 20 to 24.
    std::vector<std::uint16_t> halves(size * size * 4);
    std::vector<float> source(size);
    for (std::uint32_t x = 0; x < size; ++x)
        source[x] = x < 8 ? radiance : x < 20 ? 0.f : x < 24 ? 0.25f * float(x - 19) : 1.f;
    for (std::uint32_t y = 0; y < size; ++y)
        for (std::uint32_t x = 0; x < size; ++x)
            for (std::uint32_t channel = 0; channel < 4; ++channel)
                halves[(y * size + x) * 4 + channel] = glm::packHalf1x16(channel == 3 ? 1.f : source[x]);
    rhi::Texture input(device, device.CreateTexture(
        {size, size, rhi::Format::R16G16B16A16Float, rhi::TextureUsage::Sampled, "Sharpening test input"},
        std::as_bytes(std::span(halves))));
    rhi::Sampler sampler(device, device.CreateSampler({false, false, "Sharpening test sampler"}));
    device.GetImmediateContext().BeginFrame("UpscalerSharpeningTest");
    const auto output = sharpening.Record({.source = input.Get(), .sampler = sampler.Get(), .width = size,
                                           .height = size, .sharpness = 1.f});
    device.GetImmediateContext().Submit();
    if (!output) throw std::runtime_error("Upscaler sharpening did not run");
    const auto pixels = readFloat(output);
    if (pixels.size() != size * size * 4) throw std::runtime_error("Sharpening readback failed");
    const std::uint32_t row = size / 2;
    float lowest = radiance, highest = 0.f, change = 0.f;
    for (std::uint32_t x = 0; x < size; ++x)
    {
        const float value = pixels[(row * size + x) * 4];
        lowest = std::min(lowest, value);
        highest = std::max(highest, value);
        if (x >= 20 && x < 24) change = std::max(change, std::abs(value - source[x]));
    }
    std::cout << "Upscaler sharpening: range " << lowest << ".." << highest << ", ramp change " << change << std::endl;
    if (lowest < 0.f || highest > radiance * 1.001f) throw std::runtime_error("Upscaler sharpening rang outside the local range");
    if (change < .01f) throw std::runtime_error("Upscaler sharpening had no effect on a soft ramp");
}

// Translucent surfaces write no motion vectors. Without a reactive mask an
// upscaler reprojects them with the static background's motion and leaves a
// trail where a moving translucent object used to be. The renderer supplies
// an opaque-only snapshot so the upscaler can derive one (see FSR2.md).
template<class Device, class ReadColor>
void CheckUpscalerTranslucentTrail(PlutoGE::render::BasicRenderer &renderer, Device &device, ReadColor readColor)
{
    using namespace PlutoGE::render;
    const auto quad = [](float x0, float x1, float y0, float y1, float z) {
        return std::array<BasicVertex, 4>{{
            {{{x0,y0,z}}, {{0,0,1}}, {{0,0}}}, {{{x1,y0,z}}, {{0,0,1}}, {{1,0}}},
            {{{x1,y1,z}}, {{0,0,1}}, {{1,1}}}, {{{x0,y1,z}}, {{0,0,1}}, {{0,1}}}}};
    };
    const std::array<std::uint32_t, 6> indices{0,1,2,0,2,3};
    const auto backgroundVertices = quad(-1.f, 1.f, -1.f, 1.f, .2f);
    const auto paneVertices = quad(-.15f, .15f, -.3f, .3f, .6f);
    auto backgroundMesh = renderer.CreateMesh({backgroundVertices, indices});
    auto paneMesh = renderer.CreateMesh({paneVertices, indices});
    BasicDraw background; background.mesh = &backgroundMesh; background.emission = {.2f,.2f,.2f};
    BasicDraw pane; pane.mesh = &paneMesh; pane.surfaceType = 0; pane.alphaMode = 2;
    pane.baseColor = {0.f, 0.f, 0.f, .6f}; pane.emission = {0.f, 0.f, 3.f};
    BasicLighting lighting; lighting.ambientIntensity = lighting.directionalIntensity = 0; lighting.shadowsEnabled = false;
    renderer.SetTemporalUpscalerOptions({.technology = rhi::TemporalUpscaler::Fsr2, .autoExposure = false, .sharpness = 0});
    constexpr unsigned render = 128, output = 256;
    renderer.Resize(render, render, output, output);
    const auto halton = [](unsigned n, unsigned base) {
        float value = 0, fraction = 1;
        for (; n; n /= base) { fraction /= base; value += fraction * (n % base); }
        return value - .5f;
    };
    constexpr unsigned frames = 24;
    constexpr float step = 2.f * 3.f / render; // three render pixels per frame, in NDC
    const auto paneX = [&](unsigned frame) { return -.6f + step * float(frame); };
    for (unsigned frame = 0; frame < frames; ++frame)
    {
        pane.model[3].x = paneX(frame);
        glm::mat4 previous = pane.model;
        previous[3].x = paneX(frame ? frame - 1 : 0);
        pane.previousModel = previous;
        std::array draws{background, pane};
        rhi::TemporalUpscalerFrame upscaler;
        upscaler.contextId = 998879; upscaler.frameIndex = frame;
        upscaler.renderSize = {render, render}; upscaler.outputSize = {output, output};
        upscaler.resetHistory = frame == 0;
        upscaler.jitterPixels = {halton(frame % 32 + 1, 2), halton(frame % 32 + 1, 3)};
        renderer.Render(glm::mat4(1), lighting, draws, {}, {}, PostProcessDebugView::None, &upscaler, nullptr);
        if (!renderer.WasTemporalUpscalerEvaluated()) throw std::runtime_error("Translucent FSR dispatch failed");
    }
    const auto pixels = readColor(renderer.GetColorTexture());
    device.ReleaseTemporalUpscalerContext(998879);
    // Columns between the pane's position four frames ago and its current
    // left edge: only background should remain there.
    const auto column = [&](float ndc) { return unsigned((ndc * .5f + .5f) * output); };
    const unsigned trailBegin = column(paneX(frames - 5) - .15f), trailEnd = column(paneX(frames - 1) - .15f) - 2;
    const unsigned centreColumn = column(paneX(frames - 1));
    const auto blueAt = [&](unsigned x, unsigned y) { return int(pixels[(y * output + x) * 4 + 2]); };
    const auto redAt = [&](unsigned x, unsigned y) { return int(pixels[(y * output + x) * 4]); };
    if (blueAt(centreColumn, output / 2) < redAt(centreColumn, output / 2) + 40)
        throw std::runtime_error("Translucent pane was not composited");
    unsigned trail = 0;
    for (unsigned y = output * 3 / 8; y < output * 5 / 8; ++y)
        for (unsigned x = trailBegin; x < trailEnd; ++x)
            if (blueAt(x, y) > redAt(x, y) + 12) ++trail;
    const unsigned area = (trailEnd - trailBegin) * (output / 4);
    std::cout << "FSR translucent trail pixels=" << trail << " of " << area << std::endl;
    if (trail * 100 > area) throw std::runtime_error("Moving translucent surface left an upscaler trail");
}
