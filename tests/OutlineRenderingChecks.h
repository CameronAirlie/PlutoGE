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

// Screen-space outlines (OutlineComponent and the editor selection): a constant-width line around each layer's
// draws, composited onto the display output in that layer's style. Hidden opacity 0 must still show the visible
// silhouette, and layers in different styles must draw in the same frame.
template<class ReadPixels>
void CheckScreenOutlineRendering(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels)
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
    lighting.cameraPosition = {0,0,2};
    BasicDraw draw;
    draw.mesh = &mesh;
    draw.model = glm::translate(glm::mat4(1), glm::vec3(0,0,.5f));
    draw.emission = {0,0,1};
    struct Counts { std::size_t red = 0, yellow = 0; };
    const auto count = [&](std::span<const BasicDraw> scene, std::span<const BasicOutlineLayer> layers)
    {
        renderer.SetOutlines(layers);
        renderer.Render(glm::mat4(1), lighting, scene);
        const auto pixels = readPixels(renderer.GetColorTexture());
        Counts counts;
        for (std::size_t i = 0; i + 3 < pixels.size(); i += 4)
        {
            const int r = int(pixels[i]), g = int(pixels[i+1]), b = int(pixels[i+2]);
            counts.red += r > 150 && g < 60 && b < 60;
            counts.yellow += r > 150 && g > 150 && b < 60;
        }
        return counts;
    };
    const OutlineStyle red{.color = {1,0,0,1}, .width = 2.0f, .hiddenOpacity = 0.0f};
    std::array scene{draw};
    if (count(scene, {}).red != 0) throw std::runtime_error("Screen outline drawn with nothing outlined");
    const std::array visibleLayer{BasicOutlineLayer{red, scene}};
    const auto visible = count(scene, visibleLayer).red;
    if (visible < 20) throw std::runtime_error("Screen outline missing around a visible object: red=" + std::to_string(visible));
    // A wall in front of the object hides the outline completely at hidden opacity 0.
    BasicDraw wall = draw;
    wall.emission = {0,1,0};
    wall.model = glm::scale(glm::translate(glm::mat4(1), glm::vec3(0,0,.85f)), glm::vec3(5,5,.1f));
    std::array occluded{draw, wall};
    const std::array hiddenLayer{BasicOutlineLayer{red, std::span(occluded).first(1)}};
    if (const auto hidden = count(occluded, hiddenLayer).red; hidden != 0)
        throw std::runtime_error("Screen outline shown through a wall at hidden opacity 0: red=" + std::to_string(hidden));

    // Two objects, two styles, one frame: each keeps its own colour.
    BasicDraw left = draw, right = draw;
    left.model = glm::scale(glm::translate(glm::mat4(1), glm::vec3(-.5f,0,.5f)), glm::vec3(.6f));
    right.model = glm::scale(glm::translate(glm::mat4(1), glm::vec3(.5f,0,.5f)), glm::vec3(.6f));
    std::array pair{left, right};
    const OutlineStyle yellow{.color = {1,1,0,1}, .width = 2.0f, .hiddenOpacity = 0.0f};
    const std::array layers{BasicOutlineLayer{red, std::span(pair).first(1)}, BasicOutlineLayer{yellow, std::span(pair).last(1)}};
    const auto both = count(pair, layers);
    if (both.red < 10 || both.yellow < 10)
        throw std::runtime_error("Outline layers in different styles must draw together: red=" + std::to_string(both.red) +
                                 " yellow=" + std::to_string(both.yellow));
}
