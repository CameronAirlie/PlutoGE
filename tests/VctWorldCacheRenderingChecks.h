#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

// Rasterize and voxelize two facing surfaces. The ceiling emits red light;
// the floor must retain that local bounce with a cold and a populated cache.
// Use the reported configuration: 128 voxels, 48/144/432-unit cascades,
// 432-unit cache, 64 updates, six cones and an 81-unit trace limit.
template <class ReadPixels>
void CheckVctWorldCacheRendering(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    constexpr std::array<BasicVertex, 4> vertices = {{
        {{{-12,-12,0}}, {{0,0,1}}, {{0,0}}},
        {{{ 12,-12,0}}, {{0,0,1}}, {{1,0}}},
        {{{ 12, 12,0}}, {{0,0,1}}, {{1,1}}},
        {{{-12, 12,0}}, {{0,0,1}}, {{0,1}}}
    }};
    constexpr std::array<std::uint32_t, 6> indices{0,1,2,0,2,3};
    auto mesh = renderer.CreateMesh({vertices, indices});
    BasicDraw floor;
    floor.mesh = &mesh;
    floor.model = glm::rotate(glm::mat4(1), glm::radians(-90.0f), glm::vec3(1,0,0));
    floor.shadowBoundsRadius = 18;
    BasicDraw ceiling = floor;
    ceiling.model = glm::translate(glm::mat4(1), glm::vec3(0,3,0)) *
                    glm::rotate(glm::mat4(1), glm::radians(90.0f), glm::vec3(1,0,0));
    ceiling.shadowBoundsCenter = {0,3,0};
    ceiling.emission = {4.0f,.3f,.1f};
    std::array draws{floor, ceiling};
    BasicLighting lighting;
    lighting.cameraPosition = {0,1.5f,10};
    lighting.view = glm::lookAtRH(lighting.cameraPosition, glm::vec3(0,.8f,0), glm::vec3(0,1,0));
    lighting.ambientIntensity = lighting.directionalIntensity = 0;
    glm::mat4 projection(0);
    const float focal = 1.0f / std::tan(glm::radians(60.0f) * .5f);
    projection[0][0] = focal * float(renderer.GetHeight()) / float(renderer.GetWidth());
    projection[1][1] = focal;
    projection[2][2] = .1f / (100.0f - .1f);
    projection[2][3] = -1;
    projection[3][2] = 100.0f * .1f / (100.0f - .1f);
    BasicPostProcessEffect effect{BasicPostProcessEffectType::VCTGI};
    effect.historyOwner = &effect;
    effect.quality = 6;
    effect.parameters[0] = {48,1,.55f,81};
    effect.parameters[1] = {.35f,0,.25f,.9f}; // Disable temporal filtering for comparison.
    effect.parameters[2] = {128,3,1,1};
    effect.parameters[3] = {0,0,1,256}; // Indirect only.
    effect.parameters[4] = {0,432,64,0};
    const auto renderFrames = [&](int count)
    {
        for (int frame = 0; frame < count; ++frame)
            renderer.Render(projection * lighting.view, lighting, draws, std::span(&effect,1), {},
                            PostProcessDebugView::None, nullptr, nullptr, true, draws);
        return readPixels(renderer.GetColorTexture());
    };
    const auto reference = renderFrames(4);
    effect.parameters[4].x = 1;
    const auto cold = renderFrames(4);
    const auto populated = renderFrames(128);
    std::size_t receivers = 0;
    double energy = 0, coldError = 0, populatedError = 0;
    for (std::uint32_t y = renderer.GetHeight()/8; y < renderer.GetHeight()*7/8; ++y)
        for (std::uint32_t x = renderer.GetWidth()/4; x < renderer.GetWidth()*3/4; ++x)
        {
            const auto at = (y * renderer.GetWidth() + x) * 4;
            if (int(reference.at(at)) < 16 || int(reference.at(at)) < 3 * int(reference.at(at+1))) continue;
            ++receivers;
            energy += int(reference[at]);
            coldError += std::abs(int(cold.at(at)) - int(reference[at]));
            populatedError += std::abs(int(populated.at(at)) - int(reference[at]));
        }
    std::cout << "VCT local bounce: receivers=" << receivers << ", cold error="
              << coldError / std::max(energy,1.0) << ", populated error="
              << populatedError / std::max(energy,1.0) << '\n';
    if (receivers < 100 || coldError > energy * .1 || populatedError > energy * .1)
        throw std::runtime_error("World cache lost the local diffuse bounce");

    // Rotate away while the visible draw list loses the emitter. The full GI
    // scene stays fixed; returning must not require another cache update sweep.
    const auto originalView = lighting.view;
    effect.parameters[1].y = .92f;
    effect.parameters[3].w = 1; // Exercise multi-frame voxel jobs.
    lighting.view = glm::lookAtRH(lighting.cameraPosition, lighting.cameraPosition + glm::vec3(0,0,1), glm::vec3(0,1,0));
    for (int frame = 0; frame < 40; ++frame)
        renderer.Render(projection * lighting.view, lighting, std::span(draws.data(),1), std::span(&effect,1), {},
                        PostProcessDebugView::None, nullptr, nullptr, true, draws);
    lighting.view = originalView;
    const auto returned = renderFrames(1);
    double returnError = 0;
    for (std::size_t at = 0; at < populated.size(); at += 4)
        returnError += std::abs(int(returned.at(at)) - int(populated.at(at)));
    if (returnError > 1.0)
        throw std::runtime_error("Looking back reused camera-dependent GI");

    // A sunlit floor beneath an opaque ceiling has no direct source radiance.
    // Deliberately move the presentation shadow map away while initializing GI:
    // camera shadow coverage must never make these occluded surfaces emit light.
    draws[1].emission = glm::vec3(0);
    effect.historyOwner = &lighting; // Independent shadow-coverage fixture.
    lighting.directionalDirection = {0,-1,0}; lighting.directionalIntensity = 4;
    lighting.shadowsEnabled = true; lighting.shadowCascadeCount = 1;
    lighting.shadowResolution = 256;
    lighting.shadowMatrices.fill(glm::translate(glm::mat4(1), glm::vec3(100,100,0)));
    effect.parameters[1].y = 0;
    effect.parameters[3].w = 1;
    const auto shadowed = renderFrames(160);
    // Reinitialize from the same scene with complete presentation-shadow
    // coverage. Edge filtering may leak some light, but changing that coverage
    // must not change the persistent voxel source at all.
    effect.historyOwner = &projection;
    lighting.shadowMatrices.fill(glm::orthoRH_ZO(-20.0f,20.0f,-20.0f,20.0f,.01f,40.0f) *
        glm::lookAtRH(glm::vec3(0,20,0),glm::vec3(0),glm::vec3(0,0,1)));
    const auto covered = renderFrames(160);
    double shadowCoverageError = 0;
    for (std::size_t at = 0; at < shadowed.size(); at += 4)
        shadowCoverageError += std::abs(int(shadowed.at(at)) - int(covered.at(at)));
    std::cout << "VCT turn-back error=" << returnError << ", shadow coverage error=" << shadowCoverageError << '\n';
    if (shadowCoverageError > 1.0)
        throw std::runtime_error("Camera shadow coverage contaminated the GI source");
    // Disabling the ceiling's shadow casting must restore sunlight on the
    // floor, which can then bounce onto the ceiling. This rules out a broken
    // shadow map that merely suppresses all directional injection.
    draws[1].castsShadow = false;
    effect.historyOwner = &effect;
    const auto unshadowed = renderFrames(160);
    double blockedEnergy = 0, openEnergy = 0;
    for (std::size_t at = 0; at < covered.size(); at += 4)
    {
        blockedEnergy += int(covered.at(at));
        openEnergy += int(unshadowed.at(at));
    }
    if (openEnergy < std::max(100.0, blockedEnergy * 1.5))
        throw std::runtime_error("World shadow map suppressed unoccluded directional GI");
}

// A tiny emissive submesh of a larger mesh must deposit radiance regardless of
// whether its triangle happens to cover the center of a voxelization pixel.
template <class ReadPixels>
void CheckVctSmallEmitters(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    constexpr std::array<BasicVertex,6> vertices = {{
        {{{10,10,.5f}},{{0,0,1}},{{0,0}}},
        {{{11,10,.5f}},{{0,0,1}},{{1,0}}},
        {{{10,11,.5f}},{{0,0,1}},{{0,1}}},
        {{{0,0,.5f}},{{0,0,1}},{{0,0}}},
        {{{.02f,0,.5f}},{{0,0,1}},{{1,0}}},
        {{{0,.02f,.5f}},{{0,0,1}},{{0,1}}}
    }};
    constexpr std::array<std::uint32_t,6> indices{0,1,2,3,4,5};
    auto mesh = renderer.CreateMesh({vertices,indices});
    BasicDraw emitter;
    emitter.mesh = &mesh; emitter.firstIndex = 3; emitter.indexCount = 3;
    emitter.emission = {4,1,0}; emitter.castsShadow = false;
    BasicLighting lighting;
    lighting.ambientIntensity = lighting.directionalIntensity = 0;
    BasicPostProcessEffect effect{BasicPostProcessEffectType::VCTGI};
    effect.historyOwner = &effect; effect.quality = 6;
    effect.parameters[0] = {48,1,.55f,81};
    effect.parameters[1] = {.35f,0,.25f,.9f};
    effect.parameters[2] = {128,1,1,1};
    effect.parameters[3] = {1,0,1,256}; // Read the actual injected voxel radiance.
    int minimumRed = 255;
    for (const float phase : {.01f,.06f,.12f,.18f,.24f,.30f,.35f})
    {
        emitter.model = glm::translate(glm::mat4(1),glm::vec3(phase,phase,0));
        const float center = phase + .02f/3.0f;
        lighting.cameraPosition = {center,center,2};
        glm::mat4 projection(1);
        projection[0][0] = projection[1][1] = 40;
        projection[3][0] = projection[3][1] = -center*40;
        for (int frame=0;frame<4;++frame)
            renderer.Render(projection,lighting,std::span(&emitter,1),std::span(&effect,1));
        const auto pixels = readPixels(renderer.GetColorTexture());
        const auto at = (renderer.GetHeight()/2*renderer.GetWidth()+renderer.GetWidth()/2)*4;
        minimumRed = std::min(minimumRed,int(pixels.at(at)));
        if (int(pixels.at(at)) < 16)
            throw std::runtime_error("An emissive submesh disappeared between voxel centers");
    }
    std::cout << "VCT small emissive submeshes: all 7 alignments injected light, minimum red=" << minimumRed << '\n';
}
