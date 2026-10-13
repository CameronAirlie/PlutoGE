#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <span>
#include <stdexcept>

// Compare the original and hoisted transform paths on the actual GPU. Covers
// translated/rotated/nonuniform volumes, grazing/parallel light rays, both
// trace resolutions, the primary/light sampling limits, and sparse/dense cloud
// coverage (including octave rejection where the density is provably zero).
template <class ReadPixels>
void CheckCloudTransforms(PlutoGE::render::rhi::IRenderDevice &device,
                                PlutoGE::render::BasicRendererShaderPackage shaders,
                                const PlutoGE::render::BasicPostProcessShaderPackage &referenceCloud,
                                const PlutoGE::render::BasicPostProcessShaderPackage &referenceTrace,
                                ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    BasicRenderer optimized, reference;
    if (!optimized.Initialize(device, shaders) || !optimized.Resize(97, 65))
        throw std::runtime_error("Cloud optimization fixture initialization failed");
    shaders.postProcess[static_cast<std::size_t>(BasicPostProcessEffectType::VolumetricCloud)] = referenceCloud;
    shaders.volumetricTrace[1] = referenceTrace;
    if (!reference.Initialize(device, shaders) || !reference.Resize(97, 65))
        throw std::runtime_error("Cloud reference fixture initialization failed");
    BasicLighting lighting;
    lighting.shadowsEnabled = false;
    lighting.cameraPosition = {0, 1, 5};
    lighting.view = glm::lookAtRH(lighting.cameraPosition, glm::vec3(0, 3, 0), glm::vec3(0, 1, 0));
    const auto projection = glm::perspectiveRH_ZO(glm::radians(65.f), 97.f / 65.f, .1f, 1000.f);
    BasicPostProcessEffect cloud{BasicPostProcessEffectType::VolumetricCloud};
    cloud.parameters[0] = {1, .95f, .9f, .85f};
    cloud.parameters[1] = {.2f, .3f, .1f, .6f};
    cloud.parameters[3] = {1, .95f, .85f, 5};
    cloud.parameters[4] = {.95f, .35f, .3f, .08f};
    cloud.parameters[5] = {.18f, .25f, 0, 0};
    double maximumError = 0;
    bool scatteringObserved = false;
    for (unsigned divisor : {1u, 2u})
        for (unsigned primary : {1u, 32u, 128u})
            for (unsigned orientation = 0; orientation < 4; ++orientation)
            {
                const auto transform = glm::translate(glm::mat4(1), glm::vec3(0, 4, 0)) *
                    glm::rotate(glm::mat4(1), .37f * orientation, glm::vec3(0, 0, 1)) *
                    glm::scale(glm::mat4(1), glm::vec3(20, 8, 30));
                cloud.worldToLocal = glm::inverse(transform);
                constexpr float coverage[]{.1f, .5f, .95f, .001f};
                cloud.parameters[0].w = coverage[orientation];
                const glm::vec3 localLight = orientation == 0 ? glm::vec3(0, 1, 0) :
                    orientation == 1 ? glm::vec3(1, -1.e-5f, 0) : glm::vec3(-.25f, .8f, -.4f);
                cloud.parameters[2] = {glm::normalize(glm::mat3(transform) * localLight), .035f};
                cloud.quality = primary | ((primary == 128 ? 16u : primary == 1 ? 1u : 4u) << 8u);
                cloud.volumetricResolutionDivisor = divisor;
                optimized.Render(projection * lighting.view, lighting, {}, std::span(&cloud, 1));
                const auto actual = readPixels(optimized.GetColorTexture());
                reference.Render(projection * lighting.view, lighting, {}, std::span(&cloud, 1));
                const auto expected = readPixels(reference.GetColorTexture());
                if (actual.size() != 97 * 65 * 4 || actual.size() != expected.size())
                    throw std::runtime_error("Cloud transform readback size mismatch");
                double totalError = 0;
                for (std::size_t i = 0; i < actual.size(); ++i)
                {
                    const auto error = std::abs(int(actual[i]) - int(expected[i]));
                    maximumError = std::max(maximumError, double(error));
                    totalError += error;
                    if (i % 4 != 3 && int(expected[i]) > 5) scatteringObserved = true;
                    if (error > 2) throw std::runtime_error("Cloud transform optimization changed image coverage or scattering");
                }
                if (totalError / actual.size() > .05)
                    throw std::runtime_error("Cloud transform optimization changed mean image radiance");
            }
    if (!scatteringObserved) throw std::runtime_error("Cloud transform fixture rendered no scattering");
    std::cout << "Cloud optimization: 24 reference comparisons passed; max RGBA8 error " << maximumError << '\n';
}
