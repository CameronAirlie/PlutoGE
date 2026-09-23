#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

template<class ReadPixels>
void CheckVctIncrementalPublication(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    const auto width = renderer.GetWidth(), height = renderer.GetHeight();
    renderer.Resize(128, 96);
    constexpr std::array<BasicVertex, 4> vertices = {{
        {{{-6,-6,0}}, {{0,0,1}}, {{0,0}}}, {{{6,-6,0}}, {{0,0,1}}, {{1,0}}},
        {{{6,6,0}}, {{0,0,1}}, {{1,1}}}, {{{-6,6,0}}, {{0,0,1}}, {{0,1}}}
    }};
    constexpr std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    auto mesh = renderer.CreateMesh({vertices,indices});
    std::array<int,2> owners{};
    using Pixels = decltype(readPixels(renderer.GetColorTexture()));
    std::vector<Pixels> reference;
    std::array<std::uint64_t,2> published{};
    std::uint64_t energy = 0;
    int maximumDifference = 0;
    for (unsigned mode = 0; mode < 2; ++mode)
    {
        renderer.SetIncrementalVctPublicationEnabled(mode != 0);
        BasicDraw receiver; receiver.mesh = &mesh; receiver.baseColor = {.8f,.6f,.4f,1};
        BasicDraw emitter = receiver;
        emitter.model = glm::translate(glm::mat4(1),glm::vec3(0,0,4)) *
            glm::rotate(glm::mat4(1),glm::radians(180.0f),glm::vec3(1,0,0));
        emitter.emission = {.2f,.1f,0};
        std::vector<BasicDraw> draws{receiver,emitter};
        BasicLighting lighting;
        lighting.ambientIntensity = lighting.directionalIntensity = 0;
        lighting.cameraPosition = {0,0,2};
        lighting.view = glm::lookAtRH(lighting.cameraPosition,glm::vec3(0),glm::vec3(0,1,0));
        auto projection = glm::perspectiveRH_ZO(glm::radians(60.0f),128.0f/96.0f,100.0f,.1f);
        lighting.pointLights = {{{-2,0,1},3,{1,.2f,.1f},12}};
        BasicPostProcessEffect effect{BasicPostProcessEffectType::VCTGI};
        effect.historyOwner = &owners[mode];
        effect.parameters[0] = {16,1,.55f,12};
        effect.parameters[1] = {.35f,0,.25f,.9f};
        effect.parameters[2] = {64,2,1,1};
        effect.parameters[3] = {1,0,1,8};
        effect.parameters[4].w = 1;
        effect.parameters[5].y = 1;
        for (unsigned frame = 0; frame < 128; ++frame)
        {
            if (frame >= 24 && frame < 56) lighting.pointLights[0].position.x += .125f;
            if (frame == 56) lighting.pointLights.clear();
            if (frame == 64) lighting.pointLights = {{{1,1,1},2,{.1f,1,.2f},16}};
            if (frame == 72) effect.parameters[5].y = .5f;
            if (frame == 80) draws[1].emission *= 2;
            if (frame == 88) draws[1].model[3].z -= .5f;
            if (frame == 96) lighting.cameraPosition.x += 4; // Relocate the field, retain the view.
            if (frame == 112) draws.pop_back();
            renderer.Render(projection * lighting.view,lighting,draws,std::span(&effect,1));
            published[mode] += renderer.GetFrameStats().vctPublishedVoxels;
            auto pixels = readPixels(renderer.GetColorTexture());
            if (mode == 0) reference.push_back(std::move(pixels));
            else
            {
                if (pixels.size() != reference[frame].size()) throw std::runtime_error("VCT reference size mismatch");
                for (std::size_t at = 0; at < pixels.size(); ++at)
                {
                    maximumDifference = std::max(maximumDifference, std::abs(int(pixels[at])-int(reference[frame][at])));
                    if (at % 4 != 3) energy += int(pixels[at]);
                }
            }
        }
    }
    renderer.SetIncrementalVctPublicationEnabled(true);
    renderer.Resize(width,height);
    std::cout << "VCT full/incremental publication: max byte difference=" << maximumDifference
              << ", voxels=" << published[0] << '/' << published[1] << '\n';
    if (maximumDifference > 1 || energy == 0 || published[1] >= published[0])
        throw std::runtime_error("Incremental VCT publication differs from the full reference or did not reduce work");
}
