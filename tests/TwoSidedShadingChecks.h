#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

// Two-sided surfaces (leaves) seen from behind shade their far side. A green two-sided quad faces the camera, then the
// same quad with reversed normals shows the camera its back; under the same ambient and direct light both must shade
// alike. Before the far side was shaded, the back kept a normal facing away from the viewer: it lost its direct light
// and its specular reached the grazing Fresnel limit, mirroring the environment in grey.
template <class ReadPixels>
void CheckTwoSidedShading(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    const auto quad = [&](float normalZ)
    {
        std::array<BasicVertex, 4> vertices{};
        const std::array<std::array<float, 3>, 4> positions{{{-.8f, -.8f, .5f}, {.8f, -.8f, .5f}, {.8f, .8f, .5f}, {-.8f, .8f, .5f}}};
        for (unsigned i = 0; i < 4; ++i)
        {
            vertices[i].position = positions[i];
            vertices[i].normal = {0, 0, normalZ};
            vertices[i].tangent = {1, 0, 0, 1};
            vertices[i].uv = {.5f, .5f};
        }
        constexpr std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
        return renderer.CreateMesh({vertices, indices});
    };
    auto front = quad(1.0f), back = quad(-1.0f);

    BasicLighting lighting;
    lighting.cameraPosition = {0, 0, 2};
    lighting.shadowsEnabled = false;
    lighting.contactShadowLength = 0.0f;
    lighting.ambientIntensity = 0.5f;
    lighting.directionalIntensity = 3.0f;
    lighting.directionalDirection = glm::normalize(glm::vec3(0.0f, -0.5f, -1.0f));
    BasicDraw draw;
    draw.twoSided = true;
    draw.baseColor = {0.08f, 0.45f, 0.06f, 1.0f};
    draw.roughness = 0.5f;
    const auto sample = [&](BasicMesh &mesh)
    {
        draw.mesh = &mesh;
        renderer.Render(glm::mat4(1), lighting, std::span(&draw, 1));
        const auto image = readPixels(renderer.GetColorTexture());
        const auto at = (renderer.GetHeight() / 2 * renderer.GetWidth() + renderer.GetWidth() / 2) * 4;
        return std::array<int, 3>{int(image[at]), int(image[at + 1]), int(image[at + 2])};
    };
    const auto lit = sample(front), behind = sample(back);
    std::cout << "Two-sided shading: front " << lit[0] << ',' << lit[1] << ',' << lit[2] << ", back " << behind[0] << ','
              << behind[1] << ',' << behind[2] << '\n';
    if (lit[1] < 40 || lit[1] < lit[0] * 2)
        throw std::runtime_error("Two-sided shading scene must light the green quad's front");
    for (int channel = 0; channel < 3; ++channel)
        if (std::abs(lit[channel] - behind[channel]) > 3)
            throw std::runtime_error("A two-sided surface seen from behind must shade as its front does");
}
