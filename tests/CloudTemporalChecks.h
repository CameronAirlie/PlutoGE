#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/CloudOcclusion.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <span>
#include <stdexcept>

// Temporal reconstruction of the cloud trace (CloudTemporalFilter): a cheap
// trace that accumulates jittered frames must approach a dense reference
// trace much more closely than a single frame of the same trace, whose shared
// step offsets show as slices; a still camera must converge to a stable
// image; and the CPU cloud estimate must agree with the shader's. The
// interleaved trace (a quarter of the pixels a frame, the default) must
// converge nearly as close to the reference as tracing every pixel.
template <class ReadPixels>
void CheckCloudTemporal(PlutoGE::render::rhi::IRenderDevice &device,
                        const PlutoGE::render::BasicRendererShaderPackage &shaders, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    const auto require = [](bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    };

    {
        // EvaluateCloudLighting mirrors evaluateCloudLighting's deck estimate.
        BasicPostProcessEffect deck{BasicPostProcessEffectType::VolumetricCloud};
        deck.parameters[0] = {1, 1, 1, .8f};
        deck.parameters[1] = {0, 0, 0, 1};
        deck.parameters[2] = {0, 1, 0, .02f};
        deck.parameters[3] = {1, 1, 1, 2};
        deck.parameters[4] = {.9f, .6f, .2f, .006f};
        deck.parameters[5] = {.035f, .3f, 0, 0};
        deck.worldToLocal = glm::inverse(glm::translate(glm::mat4(1), glm::vec3(0, 600, 0)) *
                                         glm::scale(glm::mat4(1), glm::vec3(4000, 200, 4000)));
        const auto clouds = BuildCloudOcclusion(std::span(&deck, 1));
        const auto under = EvaluateCloudLighting(clouds, {0, 0, 0}, {0, 1, 0});
        const auto beside = EvaluateCloudLighting(clouds, {9000, 0, 0}, {0, 1, 0});
        const float depth = clouds.volumes[0].w * 200.f;
        require(std::abs(under.sun - std::exp(-depth)) < 1.e-4f, "CPU cloud estimate must follow the deck's optical depth");
        require(std::abs(under.sky - std::exp(-1.66f * depth)) < 1.e-4f, "CPU sky visibility must use the diffusivity factor");
        require(beside.sun == 1.f && beside.sky == 1.f, "Receivers outside a deck must be unaffected");
    }

    constexpr unsigned width = 64, height = 48;
    BasicRenderer cheap, dense, fullRate;
    if (!cheap.Initialize(device, shaders) || !cheap.Resize(width, height) ||
        !dense.Initialize(device, shaders) || !dense.Resize(width, height) ||
        !fullRate.Initialize(device, shaders) || !fullRate.Resize(width, height))
        throw std::runtime_error("Cloud temporal fixture initialization failed");
    dense.SetCloudTraceInterleave(false);
    fullRate.SetCloudTraceInterleave(false);
    BasicLighting lighting;
    lighting.shadowsEnabled = false;
    lighting.ambientIntensity = 0;
    lighting.cameraPosition = {0, 0, 0};
    lighting.view = glm::lookAtRH(lighting.cameraPosition, glm::vec3(0, 1, -1.2f), glm::vec3(0, 1, 0));
    const auto projection = glm::perspectiveRH_ZO(glm::radians(70.f), float(width) / height, 5000.f, .1f);
    BasicPostProcessEffect cloud{BasicPostProcessEffectType::VolumetricCloud};
    cloud.parameters[0] = {1, .97f, .94f, .55f};
    cloud.parameters[1] = {0, 0, 0, 1};
    cloud.parameters[2] = {glm::normalize(glm::vec3(.4f, .8f, -.3f)), .02f};
    cloud.parameters[3] = {1, .95f, .85f, 3};
    cloud.parameters[4] = {.9f, .6f, .25f, .004f};
    cloud.parameters[5] = {.025f, .3f, 0, 0};
    // A deck long enough that the cheap trace's steps span hundreds of metres.
    cloud.worldToLocal = glm::inverse(glm::translate(glm::mat4(1), glm::vec3(0, 400, -1500)) *
                                      glm::scale(glm::mat4(1), glm::vec3(4000, 300, 4000)));
    cloud.volumetricResolutionDivisor = 1;
    auto reference = cloud;
    reference.quality = 128u | (8u << 8u);
    cloud.quality = 12u | (4u << 8u);

    const auto render = [&](BasicRenderer &renderer, const BasicPostProcessEffect &effect)
    {
        renderer.Render(projection * lighting.view, lighting, {}, std::span(&effect, 1));
        return readPixels(renderer.GetColorTexture());
    };
    const auto error = [&](const auto &actual, const auto &expected)
    {
        double sum = 0;
        for (std::size_t i = 0; i < actual.size(); ++i)
            if (i % 4 != 3) sum += std::abs(int(actual[i]) - int(expected[i]));
        return sum / (width * height * 3);
    };

    // The dense reference converges too; let it settle.
    auto expected = render(dense, reference);
    for (int frame = 0; frame < 16; ++frame) expected = render(dense, reference);
    double signal = 0;
    for (std::size_t i = 0; i < expected.size(); ++i)
        if (i % 4 != 3) signal += int(expected[i]);
    require(signal / (width * height * 3) > 20, "Cloud temporal fixture rendered no clouds");

    const auto single = render(cheap, cloud); // No history yet: shared offsets, sliced.
    auto accumulated = single;
    auto previous = single;
    // An interleaved history follows a change over about 24 frames (CloudTemporalFilter::InterleavedWeight).
    for (int frame = 0; frame < 96; ++frame)
    {
        previous = accumulated;
        accumulated = render(cheap, cloud);
    }
    auto fullRateImage = render(fullRate, cloud);
    for (int frame = 0; frame < 24; ++frame) fullRateImage = render(fullRate, cloud);
    const double singleError = error(single, expected);
    const double accumulatedError = error(accumulated, expected);
    const double fullRateError = error(fullRateImage, expected);
    const double frameChange = error(accumulated, previous);
    std::cout << "Cloud temporal: single-frame error " << singleError << ", interleaved accumulated " << accumulatedError
              << ", full-rate accumulated " << fullRateError << ", frame-to-frame change " << frameChange << '\n';
    require(accumulatedError < singleError * .75, "Accumulated cloud frames must approach the dense trace");
    require(fullRateError < singleError * .75, "Full-rate accumulated cloud frames must approach the dense trace");
    require(accumulatedError < fullRateError * 1.75,
            "An interleaved cloud trace must converge nearly as close to the dense trace as a full-rate one");
    require(frameChange < 2.0, "A still camera's clouds must converge to a stable image");
}
