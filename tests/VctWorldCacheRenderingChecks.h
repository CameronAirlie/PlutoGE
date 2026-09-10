#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
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
    // Put the real surface beyond a frame's triangle budget. Its final GI
    // must match the small mesh, proving that partial draws resume at the
    // correct index and do not publish a volume before reaching the tail.
    std::vector<std::uint32_t> paddedIndices(65537 * 3, 0);
    paddedIndices.insert(paddedIndices.end(), indices.begin(), indices.end());
    auto paddedMesh = renderer.CreateMesh({vertices, paddedIndices});
    for (auto &draw : draws) draw.mesh = &paddedMesh;
    const auto chunked = renderFrames(24);
    double chunkError = 0;
    for (std::size_t at = 0; at < reference.size(); at += 4)
        chunkError += std::abs(int(chunked.at(at)) - int(reference.at(at)));
    if (chunkError > 1.0)
        throw std::runtime_error("Progressive VCT dropped triangles from a large mesh");
    for (auto &draw : draws) draw.mesh = &mesh;
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
    // Also exercise the GI shadow pass with a mesh spanning several chunks.
    for (auto &draw : draws) draw.mesh = &paddedMesh;
    const auto covered = renderFrames(160);
    for (auto &draw : draws) draw.mesh = &mesh;
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

    // Local lights must inject bounce, invalidate a stationary cache on edits,
    // and obey the injection toggle without resetting the effect owner.
    lighting.directionalIntensity = 0;
    lighting.shadowsEnabled = false;
    effect.parameters[4].w = 1;
    lighting.pointLights = {{{0,1.5f,0}, 12, {1,0,0}, 16}};
    const auto channelEnergy = [](const auto &pixels, int channel) {
        double result = 0;
        for (std::size_t at = channel; at < pixels.size(); at += 4) result += int(pixels.at(at));
        return result;
    };
    const auto point = renderFrames(160);
    if (channelEnergy(point, 0) < 100) throw std::runtime_error("Point lights did not inject VCT radiance");
    effect.parameters[5].x = 1; // 2x local bounce, preserving direct-light intensity.
    const auto boosted = renderFrames(160);
    if (channelEnergy(boosted, 0) < channelEnergy(point, 0) * 1.5)
        throw std::runtime_error("Local bounce gain did not refresh and strengthen VCT");
    effect.parameters[5].x = -1; // Zero local bounce.
    const auto muted = renderFrames(160);
    if (channelEnergy(muted, 0) > channelEnergy(point, 0) * .1)
        throw std::runtime_error("Zero local bounce retained stale radiance");
    effect.parameters[5].x = 0;
    lighting.pointLights[0].color = {0,1,0};
    const auto edited = renderFrames(160);
    if (channelEnergy(edited, 1) < 100 || channelEnergy(edited, 0) > channelEnergy(point, 0) * .1)
        throw std::runtime_error("Local light edits did not refresh VCT");
    effect.parameters[4].w = 0;
    const auto disabled = renderFrames(160);
    if (channelEnergy(disabled, 1) > channelEnergy(edited, 1) * .1)
        throw std::runtime_error("VCT ignored the local injection toggle");
    lighting.pointLights.clear();
    // Illuminate the visible floor. The narrow ceiling patch injects radiance,
    // but its bounce falls below the 8-bit readback threshold in this view.
    lighting.spotLights = {{{{0,1.5f,0},12,{1,0,0},16},{0,-1,0}}};
    effect.parameters[4].w = 1;
    const auto spot = renderFrames(160);
    std::cout << "VCT local light energy: point=" << channelEnergy(point, 0)
              << ", spot=" << channelEnergy(spot, 0) << '\n';
    if (channelEnergy(spot, 0) < 100) throw std::runtime_error("Spot lights did not inject VCT radiance");
    lighting.spotLights.clear();
    lighting.pointLights = {{{0,1.5f,0},12,{1,0,0},16}};
    const auto fullResolution = renderFrames(160);
    for (const float divisor : {2.0f, 4.0f, 1.0f})
    {
        effect.parameters[2].z = divisor;
        const auto reduced = renderFrames(2);
        const auto ratio = channelEnergy(reduced, 0) / std::max(channelEnergy(fullResolution, 0), 1.0);
        std::cout << "VCT trace divisor=" << divisor << ", energy ratio=" << ratio << std::endl;
        if (ratio < .5 || ratio > 1.5)
            throw std::runtime_error("VCT trace resolution change lost indirect lighting");
    }

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
    // A coverage-weighted 0.02-unit source needs HDR radiance to survive the
    // display readback; floating-point conservation is checked separately.
    emitter.emission = {16,4,0}; emitter.castsShadow = false;
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
        if (int(pixels.at(at)) < 1)
            throw std::runtime_error("An emissive submesh disappeared between voxel centers");
    }
    std::cout << "VCT small emissive submeshes: all 7 alignments injected light, minimum red=" << minimumRed << '\n';
}

// Read deposited radiance on a non-emissive floor. First-pass GI leaves this
// field black; the secondary pass must turn it into a coloured light source.
template <class ReadPixels>
void CheckVctSecondaryBounce(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    constexpr std::array<BasicVertex, 4> vertices = {{
        {{{-6,-6,0}}, {{0,0,1}}, {{0,0}}}, {{{6,-6,0}}, {{0,0,1}}, {{1,0}}},
        {{{6,6,0}}, {{0,0,1}}, {{1,1}}}, {{{-6,6,0}}, {{0,0,1}}, {{0,1}}}
    }};
    constexpr std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    auto mesh = renderer.CreateMesh({vertices,indices});
    BasicDraw receiver; receiver.mesh = &mesh; receiver.baseColor = {0.8f,0.2f,0.1f,1};
    BasicDraw emitter = receiver;
    emitter.model = glm::translate(glm::mat4(1),glm::vec3(0,0,4)) *
        glm::rotate(glm::mat4(1),glm::radians(180.0f),glm::vec3(1,0,0));
    emitter.emission = {4,4,4};
    std::array draws{receiver,emitter};
    BasicLighting lighting;
    lighting.ambientIntensity = lighting.directionalIntensity = 0;
    lighting.cameraPosition = {0,0,2};
    lighting.view = glm::lookAtRH(lighting.cameraPosition,glm::vec3(0),glm::vec3(0,1,0));
    glm::mat4 projection(0);
    projection[0][0] = projection[1][1] = 1;
    projection[2][2] = .1f / 99.9f; projection[2][3] = -1; projection[3][2] = 10.0f / 99.9f;
    BasicPostProcessEffect effect{BasicPostProcessEffectType::VCTGI};
    effect.historyOwner = &effect; effect.quality = 6;
    effect.parameters[0] = {16,1,.55f,12};
    effect.parameters[1] = {.35f,0,.25f,.9f};
    effect.parameters[2] = {64,1,1,1};
    effect.parameters[3] = {1,0,1,1}; // Deposited radiance, one draw/frame.
    const auto render = [&](int frames)
    {
        for (int frame = 0; frame < frames; ++frame)
            renderer.Render(projection*lighting.view,lighting,draws,std::span(&effect,1));
        const auto pixels = readPixels(renderer.GetColorTexture());
        glm::vec3 value(0); unsigned count = 0;
        for (unsigned y=renderer.GetHeight()/3;y<renderer.GetHeight()*2/3;++y)
            for (unsigned x=renderer.GetWidth()/3;x<renderer.GetWidth()*2/3;++x)
            {
                const auto at=(y*renderer.GetWidth()+x)*4;
                value += glm::vec3(pixels.at(at),pixels.at(at+1),pixels.at(at+2)); ++count;
            }
        return value / float(count);
    };
    const auto off = render(8);
    effect.parameters[5].y = 1;
    const auto on = render(8);
    const auto settled = render(24);
    std::cout << "VCT secondary deposited radiance: off=" << off.r << ", on=" << on.r
              << ", green=" << on.g << ", settled=" << settled.r << '\n';
    if (off.r > 1 || on.r < 8 || on.r < on.g*1.4f || glm::length(on-settled)>1)
        throw std::runtime_error("Secondary GI did not deposit stable material-coloured radiance");
    effect.parameters[5].y = 0;
    if (glm::length(render(1) - off) > 1)
        throw std::runtime_error("Cached secondary disable took more than one frame");
    effect.parameters[5].y = .5f;
    const auto half = render(1);
    if (half.r <= off.r + 1 || half.r >= on.r - 1 || glm::length(render(1) - half) > 1)
        throw std::runtime_error("Cached secondary strength was not linear or immediate");
    effect.parameters[5].y = 1;
    if (glm::length(render(1) - on) > 1)
        throw std::runtime_error("Cached secondary enable replayed geometry");
    // Read final received GI on a third wall, not radiance stored on the
    // first receiving floor. This exercises both legs of the secondary bounce.
    BasicDraw wall=receiver; wall.baseColor={.8f,.8f,.8f,1};
    wall.model=glm::translate(glm::mat4(1),glm::vec3(3,0,2))*
        glm::rotate(glm::mat4(1),glm::radians(-90.0f),glm::vec3(0,1,0))*
        glm::scale(glm::mat4(1),glm::vec3(.25f));
    const std::array room{receiver,emitter,wall};
    const auto originalView=lighting.view;
    lighting.view=glm::lookAtRH(lighting.cameraPosition,glm::vec3(3,0,2),glm::vec3(0,1,0));
    effect.parameters[3].x=0;
    const auto roomFrame=[&](float gain) {
        effect.parameters[5].y=gain;
        for(int frame=0;frame<24;++frame)
            renderer.Render(projection*lighting.view,lighting,room,std::span(&effect,1));
        const auto pixels=readPixels(renderer.GetColorTexture());
        const auto at=(renderer.GetHeight()/2*renderer.GetWidth()+renderer.GetWidth()/2)*4;
        return glm::vec3(int(pixels.at(at)),int(pixels.at(at+1)),int(pixels.at(at+2)));
    };
    const auto receivedOff=roomFrame(0), receivedOn=roomFrame(1);
    std::cout << "VCT secondary received light: off=" << receivedOff.r << ", on=" << receivedOn.r << '\n';
    if(receivedOn.r <= receivedOff.r+1 || glm::length(roomFrame(0)-receivedOff)>1)
        throw std::runtime_error("Secondary radiance did not reach another surface in final GI");
    lighting.view=originalView; effect.parameters[3].x=1; effect.parameters[5].y=1;
    draws[0].baseColor = {0,0,0,1};
    if (glm::length(render(12)) > 1) throw std::runtime_error("Black surface reflected secondary GI");
    draws[0].baseColor = receiver.baseColor; draws[0].metallic = 1;
    if (glm::length(render(12)) > 1) throw std::runtime_error("Metal reflected diffuse secondary GI");
    draws[0].metallic = 0;
    effect.parameters[4] = {1,48,256,0};
    if (render(32).r < 8) throw std::runtime_error("World cache lost secondary voxel radiance");
    draws[1].emission = {0,0,0};
    if (glm::length(render(40)) > 1) throw std::runtime_error("Removed emitter left secondary GI behind");
    draws[1].emission = emitter.emission;
    effect.parameters[5].y = 0;
    if (glm::length(render(12)) > 1) throw std::runtime_error("Disabling secondary GI retained its radiance");
}
