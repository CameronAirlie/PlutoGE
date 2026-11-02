#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

// Clear coat and sheen (SurfaceLayers.slang) over a dark, rough base. A smooth coat lit and seen head-on adds a
// highlight the rough base lacks; lit from the side, where the coat reflects nothing towards the view, it only takes
// light (its Fresnel). The coat is moderately smooth: a mirror-like lobe reflects the directional light as a spot
// smaller than a pixel. Sheen brightens a surface seen at a grazing angle under ambient light. Factors of zero, with
// the other layer inputs set, must leave the image exactly as without layers.
template <class ReadPixels>
void CheckSurfaceLayers(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    const auto require = [](bool condition, const std::string &message) {
        if (!condition) throw std::runtime_error(message);
    };
    std::array<BasicVertex, 4> vertices{};
    const std::array<std::array<float, 3>, 4> positions{{{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}}};
    for (unsigned i = 0; i < 4; ++i)
    {
        vertices[i].position = positions[i];
        vertices[i].normal = {0, 0, 1};
    }
    constexpr std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
    auto mesh = renderer.CreateMesh({vertices, indices});

    BasicLighting lighting;
    lighting.shadowsEnabled = false;
    lighting.contactShadowLength = 0.0f;
    const float aspect = float(renderer.GetWidth()) / float(renderer.GetHeight());
    const auto projection = glm::perspectiveRH_ZO(glm::radians(50.0f), aspect, 100.0f, 0.1f);
    BasicDraw draw;
    draw.mesh = &mesh;
    draw.twoSided = true;
    draw.baseColor = {0.05f, 0.05f, 0.05f, 1.0f};
    draw.roughness = 0.9f;
    const auto centre = [&]
    {
        renderer.Render(projection * lighting.view, lighting, std::span(&draw, 1));
        const auto image = readPixels(renderer.GetColorTexture());
        const auto at = (renderer.GetHeight() / 2 * renderer.GetWidth() + renderer.GetWidth() / 2) * 4;
        return int(image[at]) + int(image[at + 1]) + int(image[at + 2]);
    };
    const auto clearLayers = [&]
    {
        draw.clearCoat = 0.0f;
        draw.clearCoatRoughness = 0.0f;
        draw.sheenColor = glm::vec3(0.0f);
        draw.sheenRoughness = 0.5f;
    };

    // Seen head-on from 2.5 units, the quad fills the view.
    lighting.cameraPosition = {0, 0, 2.5f};
    lighting.view = glm::lookAtRH(lighting.cameraPosition, glm::vec3(0), glm::vec3(0, 1, 0));
    lighting.ambientIntensity = 0.0f;
    lighting.directionalIntensity = 2.0f;
    lighting.directionalDirection = {0, 0, -1};
    clearLayers();
    const int headOn = centre();
    draw.clearCoatRoughness = 0.3f;
    draw.sheenRoughness = 0.9f;
    const int headOnInert = centre();
    clearLayers();
    draw.clearCoat = 1.0f;
    draw.clearCoatRoughness = 0.3f;
    const int headOnCoat = centre();
    // Lit from 60 degrees: the coat's highlight points away from the camera.
    lighting.directionalDirection = glm::normalize(glm::vec3(0.0f, -0.8660254f, -0.5f));
    clearLayers();
    const int side = centre();
    draw.clearCoat = 1.0f;
    draw.clearCoatRoughness = 0.3f;
    const int sideCoat = centre();
    std::cout << "Surface layers (clear coat): head-on " << headOn << " -> " << headOnCoat << " (inert " << headOnInert
              << "), side " << side << " -> " << sideCoat << '\n';
    require(headOnInert == headOn, "Clear coat or sheen with zero factors changed the image");
    require(headOnCoat > headOn + 60, "A smooth clear coat added no highlight over a rough base");
    require(sideCoat <= side + 1, "A clear coat brightened light it reflects away from the view");

    // Ambient light, the quad seen at about 80 degrees from its normal.
    lighting.cameraPosition = {0.0f, -2.4f, 0.45f};
    lighting.view = glm::lookAtRH(lighting.cameraPosition, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0, 0, 1));
    lighting.ambientIntensity = 1.0f;
    lighting.directionalIntensity = 0.0f;
    clearLayers();
    const int grazing = centre();
    draw.sheenColor = glm::vec3(1.0f);
    draw.sheenRoughness = 0.5f;
    const int grazingSheen = centre();
    std::cout << "Surface layers (sheen): grazing ambient " << grazing << " -> " << grazingSheen << '\n';
    require(grazing > 0, "Sheen scene shows no surface at its centre");
    require(grazingSheen > grazing * 3 / 2 + 10, "Sheen did not brighten a surface seen at a grazing angle");
}
