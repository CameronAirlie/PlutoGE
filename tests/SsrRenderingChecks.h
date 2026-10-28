#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <stdexcept>
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>

// SSR effect lane 3.y (see ScreenSpaceReflections.h).
enum class SsrTechnique { Stochastic, Reference };

// A metallic floor reflecting an emissive wall, seen at a grazing angle, with the SSR effect configured for the
// stress workload (or a project's default settings).
class SsrFixture
{
public:
    SsrFixture(PlutoGE::render::BasicRenderer &renderer, bool projectSettings,
               SsrTechnique technique = SsrTechnique::Reference)
    {
        using namespace PlutoGE::render;
        constexpr std::array<BasicVertex, 4> vertices = {{
            {{{-1,-1,0}}, {{0,0,1}}, {{0,0}}},
            {{{ 1,-1,0}}, {{0,0,1}}, {{1,0}}},
            {{{ 1, 1,0}}, {{0,0,1}}, {{1,1}}},
            {{{-1, 1,0}}, {{0,0,1}}, {{0,1}}}
        }};
        constexpr std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
        mesh = renderer.CreateMesh({vertices, indices});
        lighting.requiredGeometryInputs = BasicPostProcessInput::Normal;
        lighting.cameraPosition = {0,2,4};
        lighting.view = glm::lookAtRH(lighting.cameraPosition, glm::vec3(0,0,-1), glm::vec3(0,1,0));
        lighting.ambientIntensity = 0;
        lighting.directionalIntensity = 0;
        projection[0][0] = 1.5f;
        projection[1][1] = 2.0f;
        projection[2][2] = .1f / (100.f - .1f);
        projection[2][3] = -1;
        projection[3][2] = 100.f * .1f / (100.f - .1f);
        auto &floor = draws[0];
        floor.mesh = &mesh;
        floor.model = glm::scale(glm::rotate(glm::mat4(1), glm::radians(-90.f), glm::vec3(1,0,0)), glm::vec3(4));
        floor.baseColor = {1,.1f,.1f,1};
        floor.metallic = 1;
        floor.emission = {.02f,.02f,.02f};
        auto &wall = draws[1];
        wall.mesh = &mesh;
        wall.model = glm::scale(glm::translate(glm::mat4(1), glm::vec3(0,1.5f,-2)), glm::vec3(.65f,1.5f,1));
        wall.emission = {1,1,1};
        ssr.quality = 128;
        ssr.parameters[0] = {1,20,.2f,.02f};
        ssr.parameters[1] = {.02f,5,1,8};
        if (projectSettings)
        {
            ssr.quality = 48;
            ssr.parameters[0] = {.8f,30,.35f,.08f};
            ssr.parameters[1] = {.12f,5,.75f,5};
        }
        ssr.parameters[3].y = technique == SsrTechnique::Reference ? 1.0f : 0.0f;
    }
    SsrFixture(const SsrFixture &) = delete;
    SsrFixture &operator=(const SsrFixture &) = delete;

    PlutoGE::render::BasicMesh mesh;
    PlutoGE::render::BasicLighting lighting;
    glm::mat4 projection{0.0f};
    std::array<PlutoGE::render::BasicDraw, 2> draws{};
    PlutoGE::render::BasicPostProcessEffect ssr{PlutoGE::render::BasicPostProcessEffectType::SSR};
};

template <class ReadPixels>
void CheckSsrRendering(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels,
                       PlutoGE::render::rhi::IRenderDevice *performanceDevice = nullptr, bool projectSettings = false,
                       bool extendedTiming = false, PlutoGE::render::rhi::IRenderDevice *reflectionDevice = nullptr,
                       SsrTechnique technique = SsrTechnique::Reference)
{
    using namespace PlutoGE::render;
    SsrFixture fixture(renderer, projectSettings, technique);
    const bool stochastic = technique == SsrTechnique::Stochastic;
    // Stochastic reflections integrate over frames: measure a static view once its history has settled. The
    // full-resolution reference (lane 4.w) is deterministic either way.
    const auto renderReflections = [&](const glm::mat4 &viewProjection, const BasicLighting &frameLighting,
                                       std::span<const BasicDraw> frameDraws,
                                       std::span<const BasicPostProcessEffect> effects)
    {
        const bool settles = stochastic && std::ranges::any_of(effects, [](const auto &effect)
            { return effect.type == BasicPostProcessEffectType::SSR && effect.parameters[4].w < 0.5f; });
        for (int frame = 0; frame < (settles ? 24 : 1); ++frame)
            renderer.Render(viewProjection, frameLighting, frameDraws, effects);
    };
    auto &lighting = fixture.lighting;
    const auto &projection = fixture.projection;
    auto &draws = fixture.draws;
    auto &ssr = fixture.ssr;
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
        renderReflections(projection * lighting.view, lighting, draws, std::span(&ssr,1));
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
    // A bright compact source on a rough plane exposes stochastic speckles.
    // Compare normalized high-frequency variation against the unfiltered,
    // full-resolution trace, rather than blessing a particular noise pattern.
    const auto variation = [&](const Measurement &image) {
        double residual = 0, energy = 0;
        const auto width = renderer.GetWidth();
        const auto height = renderer.GetHeight();
        for (std::uint32_t y = 1; y + 1 < height; ++y)
            for (std::uint32_t x = 1; x + 1 < width; ++x)
            {
                const auto i = y * width + x;
                const auto center = image.red[i];
                const auto neighbours = image.red[i-1] + image.red[i+1] +
                    image.red[i-width] + image.red[i+width];
                residual += std::abs(4 * center - neighbours);
                energy += 4 * center + neighbours;
            }
        return residual / std::max(energy, 1.0);
    };
    const auto filteredVariation = variation(rough);
    const auto referenceVariation = variation(reference);
    std::cout << "SSR rough variation: " << filteredVariation << " filtered, "
              << referenceVariation << " reference\n";
    if (filteredVariation > referenceVariation * 0.8)
        throw std::runtime_error("Rough SSR filtering failed to suppress stochastic speckles");
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
    if (auto *device = reflectionDevice ? reflectionDevice : performanceDevice)
    {
        constexpr std::uint32_t captureSize = 8;
        std::vector<glm::vec4> texels(2 * captureSize * 6 * captureSize, glm::vec4(.4f,.4f,.4f,1));
        rhi::Texture atlas(*device, device->CreateTexture(
            {2 * captureSize, 6 * captureSize, rhi::Format::R32G32B32A32Float,
             rhi::TextureUsage::Sampled, "SSR replacement test capture", true},
            std::as_bytes(std::span(texels))));
        auto capturedLighting = lighting;
        auto &capture = capturedLighting.iblCaptures[0];
        capture.atlas = atlas.Get(); capture.origin = {-10,-10,-10}; capture.size = {20,20,20};
        capture.intensity = 1; capture.resolution = captureSize;
        auto capturedDraws = draws;
        capturedDraws[0].baseColor = {1,1,1,1};
        capturedDraws[0].metallic = 1; capturedDraws[0].roughness = .04f;
        capturedDraws[1].baseColor = {0,0,0,1};
        capturedDraws[1].metallic = 1; capturedDraws[1].emission = {0,0,0};
        // The source radiance must be independent of its own environment
        // reflections when testing conservation against a constant capture.
        auto unlitSource = std::make_shared<ShaderGraphProgram>();
        unlitSource->data.header.y = 1;
        capturedDraws[1].shaderGraphProgram = unlitSource;
        auto replacement = ssr;
        replacement.parameters[0].x = 1;
        replacement.parameters[1].x = .02f;
        for (const float fullResolution : {0.0f, 1.0f})
        {
            replacement.parameters[4].w = fullResolution;
            renderer.Render(projection * lighting.view, capturedLighting, capturedDraws);
            const auto baseline = readPixels(renderer.GetColorTexture());
            // Use the display-space diagnostic, so the mask has the same Y
            // orientation as final scene colour on both Vulkan and OpenGL.
            renderer.Render(projection * lighting.view, capturedLighting, capturedDraws, {}, {}, PostProcessDebugView::Normal);
            const auto normals = readPixels(renderer.GetColorTexture());
            renderReflections(projection * lighting.view, capturedLighting, capturedDraws, std::span(&replacement,1));
            const auto replaced = readPixels(renderer.GetColorTexture());
            std::size_t darker = 0;
            for (std::size_t i=0; i<baseline.size(); i+=4)
                if (int(normals[i+1]) > 240 && int(replaced[i])+5 < int(baseline[i])) ++darker;
            if (darker < 5) throw std::runtime_error("SSR added dark hits instead of replacing captured specular");
            BasicPostProcessEffect ao{BasicPostProcessEffectType::SSAO};
            ao.quality = 32; ao.parameters[0] = {1.5f,.02f,1.25f,1};
            ao.parameters[1] = {2,0,.9f,1}; ao.parameters[2] = {.02f,.85f,0,0};
            const std::array occludedEffects{ao, replacement};
            renderReflections(projection * lighting.view, capturedLighting, capturedDraws, occludedEffects);
            const auto occluded = readPixels(renderer.GetColorTexture());
            int largestChange = 0;
            double totalChange = 0, signedChange = 0;
            for (std::size_t i=0; i<replaced.size(); ++i)
            {
                const int change = int(occluded[i])-int(replaced[i]);
                largestChange = std::max(largestChange, std::abs(change));
                totalChange += std::abs(change);
                signedChange += change;
            }
            const double meanChange = totalChange / double(replaced.size());
            const double meanSignedChange = signedChange / double(replaced.size());
            std::cout << "SSR with diffuse AO: mean change " << meanChange << " (signed " << meanSignedChange
                      << "), largest " << largestChange << "\n";
            // Stochastic frames differ by residual sampling noise, so they must agree on average and must not
            // shift in either direction; the deterministic reference must match exactly.
            if (stochastic ? meanChange > 1.0 || std::abs(meanSignedChange) > 0.2 : largestChange > 1)
                throw std::runtime_error("Diffuse AO invalidated the SSR specular fallback");
            // Matching capture and on-screen radiance must not brighten the
            // receiver just because both reflection sources are available.
            capturedDraws[1].emission = {.4f,.4f,.4f};
            renderReflections(projection * lighting.view, capturedLighting, capturedDraws, std::span(&replacement,1));
            const auto matching = readPixels(renderer.GetColorTexture());
            for (std::size_t i=0; i<baseline.size(); i+=4)
                if (int(normals[i+1]) > 240 && int(matching[i]) > int(baseline[i])+3)
                    throw std::runtime_error("SSR double-counted matching environmental radiance: " +
                        std::to_string(int(baseline[i])) + " -> " + std::to_string(int(matching[i])) +
                        " at pixel " + std::to_string(i/4));
            capturedDraws[1].emission = {0,0,0};
            const auto distance = replacement.parameters[0].y;
            replacement.parameters[0].y = .00001f;
            renderReflections(projection * lighting.view, capturedLighting, capturedDraws, std::span(&replacement,1));
            const auto missed = readPixels(renderer.GetColorTexture());
            for (std::size_t i=0; i<baseline.size(); ++i)
                if (std::abs(int(missed[i])-int(baseline[i])) > 1)
                    throw std::runtime_error("SSR misses changed the capture fallback");
            replacement.parameters[0].y = distance;
        }
        std::cout << "SSR capture replacement and miss preservation passed\n";
    }
    if (performanceDevice)
    {
        // The full-resolution reference, then both techniques at half resolution. Stage scopes are reported as
        // the effect records them; the parent includes every child.
        struct Configuration { const char *name; bool fullResolution; SsrTechnique technique; };
        for (const auto &configuration : {Configuration{"full-resolution reference", true, SsrTechnique::Reference},
                                          Configuration{"half-resolution reference", false, SsrTechnique::Reference},
                                          Configuration{"half-resolution stochastic", false, SsrTechnique::Stochastic}})
        {
            ssr.parameters[4].w = configuration.fullResolution ? 1.0f : 0.0f;
            ssr.parameters[3].y = configuration.technique == SsrTechnique::Reference ? 1.0f : 0.0f;
            double gpuMs = 0, effectMs = 0;
            std::vector<std::pair<std::string, double>> stages;
            int samples = 0;
            // Capture-sized comparisons need a longer warmup/sample window to
            // reduce clock-ramp and scheduling noise on short GPU workloads.
            const int warmupFrames = extendedTiming ? 256 : 16;
            const int measuredFrames = extendedTiming ? 512 : 32;
            for (int frame = 0; frame < warmupFrames + measuredFrames; ++frame)
            {
                renderer.Render(projection * lighting.view, lighting, draws, std::span(&ssr, 1));
                const auto timing = performanceDevice->GetTimingStats("Scene");
                if (frame < warmupFrames || !timing.hasGpuResult) continue;
                bool hasTrace = false, hasResolve = false;
                for (const auto &scope : timing.gpuScopes)
                {
                    if (scope.name == "RHI SSR") effectMs += scope.milliseconds;
                    if (!scope.name.starts_with("RHI SSR / ")) continue;
                    hasTrace |= scope.name == "RHI SSR / Trace";
                    hasResolve |= scope.name == "RHI SSR / Resolve";
                    const auto stage = std::ranges::find(stages, scope.name, &std::pair<std::string, double>::first);
                    if (stage == stages.end()) stages.emplace_back(scope.name, scope.milliseconds);
                    else stage->second += scope.milliseconds;
                }
                if (!hasTrace || (!configuration.fullResolution && !hasResolve))
                    throw std::runtime_error("SSR benchmark is missing stage GPU timings");
                gpuMs += timing.frameGpuMs;
                ++samples;
            }
            if (!samples) throw std::runtime_error("SSR benchmark has no GPU timings");
            std::cout << "SSR benchmark (" << configuration.name << "): " << gpuMs / samples << " ms GPU frame, "
                      << effectMs / samples << " ms reflections";
            for (const auto &[name, milliseconds] : stages)
                std::cout << ", " << milliseconds / samples << " ms " << name.substr(10);
            std::cout << " (" << samples << " samples)\n";
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
    CheckSsrRendering(reference, [&](auto texture) { return capture(expected, texture); }, nullptr, false, false, &device);
    CheckSsrRendering(renderer, [&](auto texture) { return capture(actual, texture); }, nullptr, false, false, &device);
    if (expected != actual)
        throw std::runtime_error("Specialised SSR stages changed reference pixels");
}

// Lobe-adaptive ray counts (lane 3.x) keep wide lobes exact and narrow ones close to the sixteen-ray estimate.
// With `performanceDevice`, also reports trace timings with and without them.
template <class ReadPixels>
void CheckSsrAdaptiveRays(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels,
                          PlutoGE::render::rhi::IRenderDevice *performanceDevice = nullptr, bool projectSettings = true)
{
    using namespace PlutoGE::render;
    SsrFixture fixture(renderer, projectSettings);
    auto &ssr = fixture.ssr;
    ssr.parameters[2].x = 1.0f; // trace every roughness
    const auto render = [&](bool adaptive)
    {
        ssr.parameters[3].x = adaptive ? 1.0f : 0.0f;
        renderer.Render(fixture.projection * fixture.lighting.view, fixture.lighting, fixture.draws, std::span(&ssr, 1));
        return readPixels(renderer.GetColorTexture());
    };
    for (const float roughness : {1.0f, 0.45f, 0.2f, 0.04f})
    {
        fixture.draws[0].roughness = roughness;
        const auto fixed = render(false);
        const auto adaptive = render(true);
        if (fixed.size() != adaptive.size() || fixed.empty())
            throw std::runtime_error("SSR adaptive ray readback mismatch");
        double sum = 0.0;
        int largest = 0;
        std::size_t visible = 0; // channel values changed by more than a few levels
        for (std::size_t index = 0; index < fixed.size(); ++index)
        {
            const int difference = std::abs(static_cast<int>(fixed[index]) - static_cast<int>(adaptive[index]));
            sum += difference;
            largest = std::max(largest, difference);
            visible += difference > 8 ? 1u : 0u;
        }
        const double mean = sum / static_cast<double>(fixed.size());
        std::cout << "SSR adaptive rays at roughness " << roughness << ": mean difference " << mean << ", max "
                  << largest << ", " << visible << " of " << fixed.size() << " values above 8\n";
        // Rough lobes span many pixels and keep every ray: identical. Narrower ones change by a few levels, except
        // isolated pixels on reflected silhouettes, where four rays resolve partial coverage more coarsely.
        if (roughness >= 1.0f && largest != 0)
            throw std::runtime_error("SSR adaptive rays changed a wide lobe");
        if (mean > 0.1 || visible * 200 > fixed.size())
            throw std::runtime_error("SSR adaptive rays at roughness " + std::to_string(roughness) +
                                     " differ too much (mean " + std::to_string(mean) + ", max " +
                                     std::to_string(largest) + ")");
        if (!performanceDevice)
            continue;
        // Sixteen fixed rays, sixteen adaptive, and four (the reduced-ray variant a temporal resolve integrates).
        struct Configuration { const char *name; bool adaptive; float rays; };
        for (const auto &configuration : {Configuration{"fixed", false, 16.0f}, Configuration{"adaptive", true, 16.0f},
                                          Configuration{"four-ray", true, 4.0f}})
        {
            ssr.parameters[3].x = configuration.adaptive ? 1.0f : 0.0f;
            ssr.parameters[2].y = configuration.rays;
            double traceMs = 0.0;
            int samples = 0;
            for (int frame = 0; frame < 96; ++frame)
            {
                renderer.Render(fixture.projection * fixture.lighting.view, fixture.lighting, fixture.draws, std::span(&ssr, 1));
                const auto timing = performanceDevice->GetTimingStats("Scene");
                if (frame < 32 || !timing.hasGpuResult) continue;
                for (const auto &scope : timing.gpuScopes)
                    if (scope.name == "RHI SSR / Trace") { traceMs += scope.milliseconds; ++samples; }
            }
            std::cout << "  " << configuration.name << " rays: " << (samples ? traceMs / samples : 0.0) << " ms trace\n";
        }
        ssr.parameters[2].y = 0.0f;
    }
}
