#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>
#include <vector>

// Clustered local lighting must produce the image of the unculled light loop, for perspective and orthographic
// views, with lights in front of, behind and beyond the visible surfaces.
// With `asyncCompute`, culling runs on the device's async compute queue; otherwise inline.
template <class ReadPixels>
void CheckLightClusters(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels, bool asyncCompute = false)
{
    using namespace PlutoGE::render;
    const auto require = [](bool value, const char *message) { if (!value) throw std::runtime_error(message); };
    constexpr std::uint32_t width = 160, height = 96;
    require(renderer.Resize(width, height), "Light cluster fixture resize failed");

    std::vector<BasicVertex> vertices;
    std::vector<std::uint32_t> indices;
    constexpr unsigned divisions = 32;
    for (unsigned z = 0; z <= divisions; ++z)
        for (unsigned x = 0; x <= divisions; ++x)
        {
            BasicVertex vertex{};
            vertex.position = {float(x) / divisions * 40.0f - 20.0f, 0.0f, float(z) / divisions * 40.0f - 30.0f};
            vertex.normal = {0, 1, 0};
            vertex.uv = {float(x) / divisions, float(z) / divisions};
            vertices.push_back(vertex);
        }
    for (unsigned z = 0; z < divisions; ++z)
        for (unsigned x = 0; x < divisions; ++x)
        {
            const auto a = z * (divisions + 1) + x, b = a + divisions + 1;
            indices.insert(indices.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
    auto plane = renderer.CreateMesh({vertices, indices});
    BasicDraw ground;
    ground.mesh = &plane;
    ground.baseColor = {0.8f, 0.8f, 0.8f, 1.0f};
    ground.roughness = 0.6f;

    BasicLighting lighting;
    lighting.directionalIntensity = 0.0f;
    lighting.ambientIntensity = 0.02f;
    // Points then spots fill all 32 local-light slots: near, far, behind the eye and out of view.
    for (unsigned index = 0; index < 16; ++index)
    {
        BasicPointLight light;
        light.position = {float(index % 4) * 9.0f - 13.5f, 0.6f + float(index % 3), 6.0f - float(index / 4) * 9.0f};
        light.range = 2.0f + float(index % 5);
        light.color = {0.3f + 0.7f * float(index % 2), 0.4f + 0.2f * float(index % 3), 1.0f - 0.25f * float(index % 4)};
        light.intensity = 6.0f;
        lighting.pointLights.push_back(light);
    }
    for (unsigned index = 0; index < 16; ++index)
    {
        BasicLighting::SpotLight spot;
        spot.light.position = {float(index % 4) * 8.0f - 12.0f, 3.0f, 2.0f - float(index / 4) * 8.0f};
        spot.light.range = 4.0f + float(index % 3);
        spot.light.color = {1.0f, 0.8f, 0.5f};
        spot.light.intensity = 10.0f;
        spot.direction = {0.2f * float(index % 3) - 0.2f, -1.0f, 0.1f};
        lighting.spotLights.push_back(spot);
    }

    const auto compare = [&](const glm::mat4 &projection, const glm::mat4 &view, const char *label)
    {
        lighting.view = view;
        lighting.cameraPosition = glm::vec3(glm::inverse(view)[3]);
        renderer.SetClusteredLightingEnabled(false);
        renderer.Render(projection * view, lighting, std::span(&ground, 1));
        require(renderer.GetFrameStats().clusteredLocalLights == 0, "Unclustered reference used the cluster grid");
        const auto expected = readPixels(renderer.GetColorTexture());
        renderer.SetClusteredLightingEnabled(true);
        renderer.Render(projection * view, lighting, std::span(&ground, 1));
        require(renderer.GetFrameStats().clusteredLocalLights == 32, "Local lights were not culled through clusters");
        // Each comparison changes the view or a light, so the masks must be culled again.
        require(renderer.GetFrameStats().lightClusterUpdates == 1, "A changed view or light set reused stale clusters");
        require(renderer.GetFrameStats().asyncComputeSections == (asyncCompute ? 1u : 0u),
                "Light clustering did not run on the expected queue");
        const auto actual = readPixels(renderer.GetColorTexture());
        renderer.Render(projection * view, lighting, std::span(&ground, 1));
        require(renderer.GetFrameStats().lightClusterUpdates == 0, "An unchanged view and light set re-culled clusters");
        require(renderer.GetFrameStats().asyncComputeSections == 0, "Reused light clusters still forked async compute");
        require(readPixels(renderer.GetColorTexture()) == actual, "Reused light clusters changed the image");
        require(actual.size() == std::size_t(width) * height * 4 && actual.size() == expected.size(),
                "Light cluster readback mismatch");
        std::size_t lit = 0;
        for (std::size_t index = 0; index < actual.size(); index += 4)
            lit += static_cast<int>(expected[index]) + static_cast<int>(expected[index + 1]) +
                   static_cast<int>(expected[index + 2]) > 24 ? 1 : 0;
        require(lit > width * height / 8, "Light cluster fixture is not lit by its local lights");
        for (std::size_t index = 0; index < actual.size(); ++index)
            if (actual[index] != expected[index])
            {
                std::cerr << label << ": clustered lighting differs at byte " << index << " ("
                          << int(actual[index]) << " vs " << int(expected[index]) << ")\n";
                throw std::runtime_error("Clustered lighting changed the image");
            }
        std::cout << label << " clustered lighting matches the unculled loop (" << lit << " lit pixels"
                  << (asyncCompute ? ", culled on async compute" : "") << ")\n";
    };

    // Reversed-Z projections, as the renderer's cameras use.
    const auto view = glm::lookAtRH(glm::vec3(0.0f, 5.0f, 9.0f), glm::vec3(0.0f, 0.0f, -6.0f), glm::vec3(0, 1, 0));
    glm::mat4 perspective(0.0f);
    constexpr float nearPlane = 0.1f, farPlane = 200.0f;
    const float focal = 1.0f / std::tan(glm::radians(60.0f) * 0.5f);
    perspective[0][0] = focal / (float(width) / float(height));
    perspective[1][1] = focal;
    perspective[2][2] = nearPlane / (farPlane - nearPlane);
    perspective[2][3] = -1.0f;
    perspective[3][2] = farPlane * nearPlane / (farPlane - nearPlane);
    compare(perspective, view, "Perspective");
    // A moved light must re-cull rather than reuse the previous masks.
    lighting.pointLights[5].position += glm::vec3(1.5f, 0.0f, -0.5f);
    compare(perspective, view, "Moved light");
    compare(glm::orthoRH_ZO(-22.0f, 22.0f, -13.0f, 13.0f, 80.0f, -20.0f), view, "Orthographic");
}
