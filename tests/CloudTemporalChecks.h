#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/CloudOcclusion.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <span>
#include <stdexcept>
#include <vector>

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
        deck.parameters[5] = {.035f, .3f, .2f, 0};
        deck.parameters[6].w = .2f; // RGB ambient: lanes 4.z, 5.z, 6.w
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
    cloud.parameters[5] = {.025f, .3f, .25f, 0};
    cloud.parameters[6].w = .25f; // RGB ambient: lanes 4.z, 5.z, 6.w
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

    // Thin occluders in front of the deck, swaying a pixel or two a frame like leaves against the sky. The cloud
    // history reprojects at the clouds' distance, and a neighbourhood of sky and leaf pixels clips nothing away, so
    // without depth-aware resolving the clouds' radiance smears across the leaves and the leaves cut blurred holes
    // into the clouds. Each leaf emits pure blue: any red on a leaf pixel came from the clouds.
    constexpr std::array<BasicVertex, 4> vertices = {{
        {{{-1, -1, 0}}, {{0, 0, 1}}, {{0, 0}}},
        {{{1, -1, 0}}, {{0, 0, 1}}, {{1, 0}}},
        {{{1, 1, 0}}, {{0, 0, 1}}, {{1, 1}}},
        {{{-1, 1, 0}}, {{0, 0, 1}}, {{0, 1}}}}};
    constexpr std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
    for (unsigned divisor : {1u, 2u})
    {
        BasicRenderer swaying;
        if (!swaying.Initialize(device, shaders) || !swaying.Resize(width, height))
            throw std::runtime_error("Cloud occlusion fixture initialization failed");
        auto mesh = swaying.CreateMesh({vertices, indices});
        auto deck = cloud;
        deck.volumetricResolutionDivisor = divisor;
        // A pixel at 10 m is 2 * 10 * tan(35 deg) / height metres tall; leaves are about one pixel wide.
        const float pixelSize = 20.f * std::tan(glm::radians(35.f)) / height;
        const glm::mat4 cameraToWorld = glm::inverse(lighting.view);
        const auto leaves = [&](int frame)
        {
            std::vector<BasicDraw> draws;
            const float sway = std::round(2.f * std::sin(frame * 1.3f)) * pixelSize;
            for (int column = -8; column <= 8; ++column)
            {
                BasicDraw leaf;
                leaf.mesh = &mesh;
                leaf.baseColor = {0, 0, 0, 1};
                leaf.emission = {0, 0, 1};
                leaf.model = cameraToWorld *
                    glm::translate(glm::mat4(1), glm::vec3(column * 4.f * pixelSize + sway, 0, -10)) *
                    glm::scale(glm::mat4(1), glm::vec3(.5f * pixelSize, 12, 1));
                draws.push_back(leaf);
            }
            return draws;
        };
        const auto renderSwaying = [&](int frame, bool clouds)
        {
            const auto draws = leaves(frame);
            swaying.Render(projection * lighting.view, lighting, draws,
                           clouds ? std::span(&deck, 1) : std::span<const BasicPostProcessEffect>{});
            return readPixels(swaying.GetColorTexture());
        };
        constexpr int frames = 48;
        auto image = renderSwaying(0, true);
        for (int frame = 1; frame < frames; ++frame) image = renderSwaying(frame, true);
        // The same leaves without clouds mark which pixels they cover in the last frame; the history is gone too.
        const auto mask = renderSwaying(frames - 1, false);
        double leafRed = 0, skyRed = 0, clearRed = 0;
        int leafPixels = 0, skyPixels = 0;
        for (std::size_t pixel = 0; pixel < std::size_t(width) * height; ++pixel)
        {
            const bool leaf = int(mask[pixel * 4 + 2]) > 128;
            (leaf ? leafRed : skyRed) += int(image[pixel * 4]);
            (leaf ? leafPixels : skyPixels) += 1;
            if (!leaf) clearRed += int(expected[pixel * 4]);
        }
        require(leafPixels > 100 && skyPixels > 100, "Cloud occlusion fixture must cover part of the sky with leaves");
        leafRed /= leafPixels;
        skyRed /= skyPixels;
        clearRed /= skyPixels;
        std::cout << "Cloud occlusion (divisor " << divisor << "): mean red on leaves " << leafRed << ", between leaves "
                  << skyRed << " (unoccluded clouds " << clearRed << ")\n";
        require(leafRed < 3.0, "Cloud radiance must not smear onto opaque foreground leaves");
        require(skyRed > clearRed * .85, "Swaying leaves must not cut holes into the clouds between them");
    }
}
