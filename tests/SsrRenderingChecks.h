#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <array>
#include <algorithm>
#include <cstdint>
#include <vector>
#include <stdexcept>
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>

template <class ReadPixels>
void CheckSsrRendering(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels,
                       PlutoGE::render::rhi::IRenderDevice *performanceDevice = nullptr, bool projectSettings = false)
{
    using namespace PlutoGE::render;
    constexpr std::array<BasicVertex, 4> vertices = {{
        {{{-1,-1,0}}, {{0,0,1}}, {{0,0}}},
        {{{ 1,-1,0}}, {{0,0,1}}, {{1,0}}},
        {{{ 1, 1,0}}, {{0,0,1}}, {{1,1}}},
        {{{-1, 1,0}}, {{0,0,1}}, {{0,1}}}
    }};
    constexpr std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    auto mesh = renderer.CreateMesh({vertices, indices});
    BasicLighting lighting;
    lighting.cameraPosition = {0,2,4};
    lighting.view = glm::lookAtRH(lighting.cameraPosition, glm::vec3(0,0,-1), glm::vec3(0,1,0));
    lighting.ambientIntensity = 0;
    lighting.directionalIntensity = 0;
    glm::mat4 projection(0);
    projection[0][0] = 1.5f;
    projection[1][1] = 2.0f;
    projection[2][2] = .1f / (100.f - .1f);
    projection[2][3] = -1;
    projection[3][2] = 100.f * .1f / (100.f - .1f);
    BasicDraw floor;
    floor.mesh = &mesh;
    floor.model = glm::scale(glm::rotate(glm::mat4(1), glm::radians(-90.f), glm::vec3(1,0,0)), glm::vec3(4));
    floor.baseColor = {1,.1f,.1f,1};
    floor.metallic = 1;
    floor.emission = {.02f,.02f,.02f};
    BasicDraw wall;
    wall.mesh = &mesh;
    wall.model = glm::scale(glm::translate(glm::mat4(1), glm::vec3(0,1.5f,-2)), glm::vec3(.65f,1.5f,1));
    wall.emission = {1,1,1};
    std::array draws{floor,wall};
    BasicPostProcessEffect ssr{BasicPostProcessEffectType::SSR};
    ssr.quality = 128;
    ssr.parameters[0] = {1,20,.2f,.02f};
    ssr.parameters[1] = {.02f,5,1,8};
    if (projectSettings)
    {
        ssr.quality = 48;
        ssr.parameters[0] = {.8f,30,.35f,.08f};
        ssr.parameters[1] = {.12f,5,.75f,5};
    }
    struct Measurement
    {
        std::array<std::uint64_t,3> energy{};
        std::vector<int> red;
    };
    const auto measure = [&](float roughness, float metallic)
    {
        draws[0].roughness = roughness;
        draws[0].metallic = metallic;
        renderer.Render(projection * lighting.view, lighting, draws);
        const auto baseline = readPixels(renderer.GetColorTexture());
        const auto normals = readPixels(renderer.GetNormalTexture());
        renderer.Render(projection * lighting.view, lighting, draws, std::span(&ssr,1));
        const auto reflected = readPixels(renderer.GetColorTexture());
        Measurement result;
        result.red.resize(baseline.size()/4);
        for (std::size_t i=0; i<baseline.size(); i+=4)
            if (int(normals[i+1]) > 240)
                for (int c=0; c<3; ++c)
                {
                    if (int(reflected[i+c])+1 < int(baseline[i+c]))
                        throw std::runtime_error("SSR removed existing surface lighting");
                    const int contribution = std::max(int(reflected[i+c])-int(baseline[i+c]),0);
                    result.energy[c] += contribution;
                    if (c == 0) result.red[i/4] = contribution;
                }
        return result;
    };
    const auto smooth = measure(.04f,1);
    const auto rough = measure(.6f,1);
    const auto dielectric = measure(.5f,0);
    std::cout << "SSR energy: " << smooth.energy[0] << ", " << rough.energy[0] << ", " << dielectric.energy[0] << std::endl;
    if (smooth.energy[0] < 100 || rough.energy[0] < 100 || dielectric.energy[0] < 10)
        throw std::runtime_error("SSR lost smooth, moderately rough, or dielectric reflections");
    std::size_t spread = 0;
    for (std::size_t i=0; i<rough.red.size(); ++i)
        if (smooth.red[i] <= 1 && rough.red[i] >= 3) ++spread;
    if (spread < 5)
        throw std::runtime_error("Rough SSR lost its broader lobe");
    if (rough.energy[0] < rough.energy[1]*2)
        throw std::runtime_error("SSR ignored metallic albedo tint");
    ssr.parameters[4].w = 1.0f;
    const auto reference = measure(.6f, 1);
    if (rough.energy[0] < reference.energy[0] * 0.65 ||
        rough.energy[0] > reference.energy[0] * 1.35)
        throw std::runtime_error("Half-resolution SSR changed rough reflection energy excessively");
    ssr.parameters[4].w = 0.0f;
    // With direct and ambient lighting disabled, fully rough receivers must
    // preserve their baseline even when a bright reflected source is visible.
    for (const bool fullResolution : {false, true})
        for (const float metallic : {0.0f, 1.0f})
        {
            ssr.parameters[4].w = fullResolution ? 1.0f : 0.0f;
            const auto fullyRough = measure(1.0f, metallic);
            if (fullyRough.energy != std::array<std::uint64_t, 3>{})
                throw std::runtime_error("SSR contributed to a fully rough unlit surface");
        }
    ssr.parameters[4].w = 0.0f;
    draws[0].roughness = .6f;
    const auto configuredRefinements = ssr.parameters[1].w;
    for (const float refinements : {0.0f, 1.0f, 5.0f, 8.0f})
        for (const float fullResolution : {0.0f, 1.0f})
        {
            ssr.parameters[1].w = refinements;
            ssr.parameters[4].w = fullResolution;
            measure(.6f, 1);
        }
    ssr.parameters[1].w = configuredRefinements;
    if (performanceDevice)
    {
        for (const bool fullResolution : {true, false})
        {
            ssr.parameters[4].w = fullResolution ? 1.0f : 0.0f;
            double gpuMs = 0, effectMs = 0, traceMs = 0, resolveMs = 0;
            int samples = 0;
            for (int frame = 0; frame < 48; ++frame)
            {
                renderer.Render(projection * lighting.view, lighting, draws, std::span(&ssr, 1));
                const auto timing = performanceDevice->GetTimingStats("Scene");
                if (frame < 16 || !timing.hasGpuResult) continue;
                bool hasTrace = false, hasResolve = false;
                for (const auto &scope : timing.gpuScopes)
                {
                    if (scope.name == "RHI SSR") effectMs += scope.milliseconds;
                    if (scope.name == "RHI SSR / Trace") { traceMs += scope.milliseconds; hasTrace = true; }
                    if (scope.name == "RHI SSR / Resolve") { resolveMs += scope.milliseconds; hasResolve = true; }
                }
                if (!hasTrace || (!fullResolution && !hasResolve))
                    throw std::runtime_error("SSR benchmark is missing stage GPU timings");
                gpuMs += timing.frameGpuMs;
                ++samples;
            }
            if (!samples) throw std::runtime_error("SSR benchmark has no GPU timings");
            std::cout << "SSR benchmark (" << (fullResolution ? "full-resolution reference" : "half-resolution resolve")
                      << "): " << gpuMs / samples << " ms GPU frame, "
                      << effectMs / samples << " ms reflections, " << traceMs / samples << " ms trace, "
                      << resolveMs / samples << " ms resolve (" << samples << " samples)\n";
        }
    }
}

// Compare the specialised stages with the legacy shader package fallback.
// Include all G-buffer and output readbacks from the behavioural checks.
template <class ReadPixels>
void CheckSsrStageSpecialisation(PlutoGE::render::BasicRenderer &renderer,
                               PlutoGE::render::rhi::IRenderDevice &device,
                               PlutoGE::render::BasicRendererShaderPackage shaders,
                               ReadPixels readPixels)
{
    std::vector<unsigned char> expected, actual;
    const auto capture = [&](auto &destination, auto texture)
    {
        auto pixels = readPixels(texture);
        for (const auto value : pixels) destination.push_back(static_cast<unsigned char>(value));
        return pixels;
    };
    shaders.ssrStages = {};
    PlutoGE::render::BasicRenderer reference;
    if (!reference.Initialize(device, shaders) || !reference.Resize(renderer.GetWidth(), renderer.GetHeight()))
        throw std::runtime_error("Cannot initialise SSR reference renderer");
    CheckSsrRendering(reference, [&](auto texture) { return capture(expected, texture); });
    CheckSsrRendering(renderer, [&](auto texture) { return capture(actual, texture); });
    if (expected != actual)
        throw std::runtime_error("Specialised SSR stages changed reference pixels");
}
