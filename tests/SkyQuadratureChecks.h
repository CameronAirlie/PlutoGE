#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

template<class Device, class ReadPixels>
void CheckSkyQuadrature(Device &device, const PlutoGE::render::BasicRendererShaderPackage &shaders, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    BasicRenderer reference, optimized;
    auto referenceShaders = shaders;
    referenceShaders.skyQuadrature = {};
    if (!reference.Initialize(device, referenceShaders) || !optimized.Initialize(device, shaders))
        throw std::runtime_error("Sky comparison initialization failed");
    reference.Resize(96, 64); optimized.Resize(96, 64);
    std::vector<BasicVertex> vertices(4);
    vertices[0].position = {-1, -1, .5f}; vertices[1].position = {1, -1, .5f};
    vertices[2].position = {1, 1, .5f}; vertices[3].position = {-1, 1, .5f};
    vertices[0].normal = {0, 1, 0}; vertices[1].normal = {1, 0, 0};
    vertices[2].normal = {0, -1, 0}; vertices[3].normal = {0, 0, 1};
    const std::vector<std::uint32_t> indices{0, 1, 2, 0, 2, 3};
    auto mesh = optimized.CreateMesh({vertices, indices});
    BasicDraw draw; draw.mesh = &mesh; draw.castsShadow = false;
    BasicLighting lighting; lighting.directionalIntensity = 0; lighting.shadowsEnabled = false;
    lighting.cameraPosition = {0, 0, 3}; lighting.physicalSkyEnabled = true;
    unsigned maximumDifference = 0;
    for (int scenario = 0; scenario < 12; ++scenario)
    {
        const float height = -.8f + .15f * scenario;
        lighting.physicalSkyParameters[0] = glm::vec4(glm::normalize(glm::vec3(.5f, height, .3f)), 1);
        lighting.physicalSkyParameters[1] = {1, .98f, .95f, .65f};
        lighting.physicalSkyParameters[2] = {.55f, .65f, .95f, .65f};
        lighting.physicalSkyParameters[3] = {.012f, .015f, .02f, 1};
        lighting.physicalSkyParameters[4] = {8, .27f, .035f, 0};
        lighting.physicalSkyParameters[5] = {.8f, .26f, 0, 0};
        lighting.physicalSkyExposure = scenario % 2 ? .5f : 1.0f;
        draw.metallic = scenario % 3 == 0 ? 1.0f : 0.0f;
        draw.roughness = .1f + .07f * scenario;
        reference.Render(glm::mat4(1), lighting, std::span(&draw, 1));
        const auto expected = readPixels(reference.GetColorTexture(), 96, 64);
        optimized.Render(glm::mat4(1), lighting, std::span(&draw, 1));
        const auto actual = readPixels(optimized.GetColorTexture(), 96, 64);
        lighting.geometryDiagnosticMode = GeometryDiagnosticMode::ReferenceSky;
        optimized.Render(glm::mat4(1), lighting, std::span(&draw, 1));
        if (readPixels(optimized.GetColorTexture(), 96, 64) != expected)
            throw std::runtime_error("Runtime reference-sky diagnostic differs from reference shader package");
        lighting.geometryDiagnosticMode = GeometryDiagnosticMode::None;
        if (actual.empty() || actual.size() != expected.size()) throw std::runtime_error("Sky comparison readback failed");
        for (std::size_t i = 0; i < actual.size(); ++i)
            maximumDifference = std::max(maximumDifference, unsigned(std::abs(int(actual[i]) - int(expected[i]))));
        if (scenario == 5)
        {
            optimized.Render(glm::mat4(1), lighting, std::span(&draw, 1), {}, {},
                             PostProcessDebugView::DirectionalShadowMaskFiltered);
            (void)readPixels(optimized.GetColorTexture(), 96, 64);
            optimized.Render(glm::mat4(1), lighting, std::span(&draw, 1));
            if (readPixels(optimized.GetColorTexture(), 96, 64) != actual)
                throw std::runtime_error("Leaving a diagnostic view changed normal rendering");
        }
    }
    if (maximumDifference > 1) throw std::runtime_error("Precomputed sky differs from reference by more than one output code value");
    std::cout << "Sky quadrature: 12 sky/material states, maximum output difference " << maximumDifference << "/255\n";
}
