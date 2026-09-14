#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <array>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

template<class ReadPixels>
void CheckOutlineRendering(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    std::array<BasicVertex, 8> vertices{};
    for (unsigned i = 0; i < 8; ++i)
    {
        const glm::vec3 p(i & 1 ? .2f : -.2f, i & 2 ? .2f : -.2f, i & 4 ? .2f : -.2f);
        vertices[i].position = {p.x, p.y, p.z};
        const auto n = glm::normalize(p);
        vertices[i].normal = {n.x, n.y, n.z};
    }
    std::array<std::uint32_t, 36> indices{0,2,3,0,3,1,4,5,7,4,7,6,0,1,5,0,5,4,2,6,7,2,7,3,0,4,6,0,6,2,1,3,7,1,7,5};
    auto mesh = renderer.CreateMesh({vertices, indices});
    BasicLighting lighting;
    lighting.ambientIntensity = lighting.directionalIntensity = 0;
    glm::mat4 projection(1);
    // RHI uses reversed depth: larger Z is nearer.
    lighting.cameraPosition = {0,0,2};
    BasicDraw draw;
    draw.mesh = &mesh;
    draw.model = glm::translate(glm::mat4(1), glm::vec3(0,0,.5f));
    draw.emission = {0,0,1};
    draw.outlineColor = {1,0,0};
    const auto countRed = [&](std::span<const BasicDraw> draws)
    {
        renderer.Render(projection, lighting, draws);
        const auto pixels = readPixels(renderer.GetColorTexture());
        std::size_t red = 0, blue = 0;
        for (std::size_t i = 0; i + 3 < pixels.size(); i += 4)
        {
            red += int(pixels[i]) > 200 && int(pixels[i+2]) < 20;
            blue += int(pixels[i+2]) > 200 && int(pixels[i]) < 20;
        }
        return std::array{red, blue};
    };
    if (countRed(std::span(&draw,1))[0] != 0) throw std::runtime_error("Disabled outline rendered");
    draw.outlineWidth = .12f;
    auto result = countRed(std::span(&draw,1));
    if (result[0] < 20 || result[1] < 20) throw std::runtime_error("Outline missing or overwrote surface: red=" + std::to_string(result[0]) + " blue=" + std::to_string(result[1]));
    auto models = std::make_shared<std::vector<glm::mat4>>();
    for (float x : {-.45f,.45f})
        models->push_back(glm::scale(glm::translate(glm::mat4(1), glm::vec3(x,0,.5f)), glm::vec3(.7f,1,1)));
    draw.instanceModels = models;
    result = countRed(std::span(&draw,1));
    if (result[0] < 40 || result[1] < 40) throw std::runtime_error("Instanced outline missing");
    BasicDraw occluder = draw;
    occluder.instanceModels.reset();
    occluder.outlineWidth = 0;
    occluder.model = glm::scale(glm::translate(glm::mat4(1), glm::vec3(0,0,.85f)), glm::vec3(5,5,.1f));
    std::array draws{draw, occluder};
    if (countRed(draws)[0] != 0) throw std::runtime_error("Outline visible through foreground mesh");
}
