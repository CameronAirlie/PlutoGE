#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

// The same image checks run against real OpenGL and Vulkan devices.
template <class ReadPixels>
void CheckGlassRendering(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    const auto require = [](bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    };
    constexpr std::array<BasicVertex, 4> vertices = {{
        {{{-.9f,-.9f,0}}, {{0,0,1}}, {{0,0}}},
        {{{ .9f,-.9f,0}}, {{0,0,1}}, {{1,0}}},
        {{{ .9f, .9f,0}}, {{0,0,1}}, {{1,1}}},
        {{{-.9f, .9f,0}}, {{0,0,1}}, {{0,1}}}
    }};
    constexpr std::array<std::uint32_t, 6> indices{0,1,2,0,2,3};
    auto mesh = renderer.CreateMesh({vertices, indices});
    BasicLighting lighting;
    lighting.cameraPosition = {0,0,2};
    lighting.ambientIntensity = 0;
    lighting.directionalIntensity = 0;
    BasicDraw background;
    background.mesh = &mesh;
    background.model[3].z = .2f;
    background.emission = {.6f,.4f,.2f};
    BasicDraw pane;
    pane.mesh = &mesh;
    pane.model[3].z = .6f;
    pane.surfaceType = 1;
    pane.transmission = 1;
    pane.ior = 1;
    pane.roughness = .04f;
    pane.thickness = 1;
    pane.twoSided = true;
    const auto render = [&](std::span<const BasicDraw> draws,
                            std::span<const BasicPostProcessEffect> effects = {})
    {
        renderer.Render(glm::mat4(1), lighting, draws, effects);
        const auto pixels = readPixels(renderer.GetColorTexture());
        const auto center = (renderer.GetHeight()/2 * renderer.GetWidth() + renderer.GetWidth()/2) * 4;
        require(pixels.size() >= center + 4, "Glass readback size mismatch");
        return std::array<int,3>{int(pixels[center]),int(pixels[center+1]),int(pixels[center+2])};
    };
    const auto reference = render(std::span(&background,1));
    const auto opaqueNormals = readPixels(renderer.GetNormalTexture());
    const auto opaqueMaterial = readPixels(renderer.GetMaterialTexture());
    std::array draws{pane, background}; // Deliberately submit glass first.
    const auto clear = render(draws);
    for (int c=0;c<3;++c)
        require(std::abs(clear[c]-reference[c]) <= 3, "Clear glass did not preserve its background");
    require(readPixels(renderer.GetNormalTexture()) == opaqueNormals, "Glass overwrote opaque normals");
    require(readPixels(renderer.GetMaterialTexture()) == opaqueMaterial, "Glass overwrote opaque material");
    draws[0].attenuationColor = {.2f,1,1};
    const auto tinted = render(draws);
    require(tinted[0] + 20 < clear[0] && std::abs(tinted[1]-clear[1]) <= 3,
            "Glass absorption did not tint transmitted light");
    draws[0].twoSided = false;
    require(render(draws) == tinted, "Single-sided glass culled its front face");
    draws[0].twoSided = true;
    draws[0].baseColor.a = .5f;
    const auto partial = render(draws);
    require(partial[0] > tinted[0] && partial[0] < clear[0], "Partial coverage did not blend glass");
    draws[0].baseColor.a = 1;
    draws[0].thickness = 2;
    const auto thicker = render(draws);
    require(thicker[0] < tinted[0], "Increasing glass thickness did not increase absorption");
    draws[0].baseColor.a = 0;
    require(render(draws) == reference, "Zero coverage glass changed scene color");
    draws[0].baseColor.a = 1;
    draws[0].model[3].z = .1f;
    require(render(draws) == reference, "Glass was not occluded by opaque depth");
    // A fully transmitting pane in front must preserve the tinted pane behind.
    auto rear = pane;
    rear.attenuationColor = {.2f,1,1};
    rear.model[3].z = .4f;
    std::array layers{pane, background, rear};
    const auto layered = render(layers);
    require(std::abs(layered[0]-tinted[0]) <= 3, "Front glass erased a previously rendered layer");
    std::swap(layers[0], layers[2]);
    require(render(layers) == layered, "Glass depended on submission order");
    auto instances = std::make_shared<std::vector<glm::mat4>>();
    instances->push_back(pane.model);
    instances->push_back(rear.model);
    draws = {rear, background};
    draws[0].instanceModels = instances;
    require(render(draws)[0] < tinted[0], "Glass instances were not individually composited");
    const std::array effects{BasicPostProcessEffect{BasicPostProcessEffectType::ToneMapping}};
    const auto mappedReference = render(std::span(&background,1), effects);
    std::array clearDraws{pane,background};
    const auto mappedGlass = render(clearDraws, effects);
    for (int c=0;c<3;++c)
        require(std::abs(mappedGlass[c]-mappedReference[c]) <= 3, "Glass composited after tone mapping");
    auto blended = pane;
    blended.surfaceType = 0;
    blended.alphaMode = 2;
    blended.baseColor.a = .5f;
    blended.emission = {0,0,.8f};
    std::array blendDraws{blended,background};
    const auto blendedColor = render(blendDraws);
    require(blendedColor[0] < clear[0] && blendedColor[2] > clear[2],
            "Standard alpha blend material was not composited");
    // Refraction must move a color boundary, rather than merely change Fresnel brightness.
    auto angledVertices = vertices;
    for (auto &vertex : angledVertices) vertex.normal = {.7071f,0,.7071f};
    auto angledMesh = renderer.CreateMesh({angledVertices, indices});
    auto left = background;
    left.model = glm::translate(glm::mat4(1), glm::vec3(-.45f,0,.2f)) *
                 glm::scale(glm::mat4(1), glm::vec3(.5f,1,1));
    left.emission = {.8f,0,0};
    auto right = background;
    right.model = glm::translate(glm::mat4(1), glm::vec3(.45f,0,.2f)) *
                  glm::scale(glm::mat4(1), glm::vec3(.5f,1,1));
    right.emission = {0,.8f,0};
    auto refractor = pane;
    refractor.mesh = &angledMesh;
    refractor.thickness = .5f;
    std::array boundary{refractor,left,right};
    const auto straight = render(boundary);
    boundary[0].ior = 1.5f;
    const auto refracted = render(boundary);
    require(straight[1] > straight[0] + 30 && refracted[0] > refracted[1] + 30,
            "IOR did not refract the background color boundary");
    boundary[0].ior = 1;
    boundary[0].roughness = 1;
    const auto frosted = render(boundary);
    require(frosted[0] > straight[0] + 20, "Roughness did not blur transmission");
    boundary[0].ior = 1.5f;
    boundary[0].roughness = .04f;
    boundary[1].model[3].z = .8f;
    const auto foreground = render(boundary);
    require(foreground[1] > foreground[0] + 30, "Glass refracted opaque foreground into the background");
    require(renderer.Resize(48,32), "Glass resize failed");
    require(render(layers)[0] > 0, "Glass failed after resize");
    require(renderer.Resize(96,64), "Glass test restore resize failed");
}
