#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

// See-through translucency (MaterialConfig::seeThrough): a Blend draw behind an opaque one is hidden by the depth test,
// unless it is see-through; a see-through draw that is not Blend changes nothing.
template<class ReadPixels>
void CheckSeeThroughRendering(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    const auto require = [](bool ok, const char *message) { if (!ok) throw std::runtime_error(message); };
    constexpr std::array<BasicVertex, 4> vertices = {{
        {{{-.9f,-.9f,0}}, {{0,0,1}}, {{0,0}}}, {{{.9f,-.9f,0}}, {{0,0,1}}, {{1,0}}},
        {{{.9f,.9f,0}}, {{0,0,1}}, {{1,1}}}, {{{-.9f,.9f,0}}, {{0,0,1}}, {{0,1}}}
    }};
    constexpr std::array<std::uint32_t, 6> indices{0,1,2,0,2,3};
    auto mesh = renderer.CreateMesh({vertices, indices});
    BasicLighting lighting; lighting.directionalIntensity = 0; lighting.ambientIntensity = .8f;
    lighting.cameraPosition = {0,0,2};
    // A grey opaque quad; translucent red quads either side of it in depth.
    // Depths inside both clip conventions (Vulkan's 0..1 and OpenGL's -1..1).
    BasicDraw blocker; blocker.mesh = &mesh; blocker.model[3].z = .5f; blocker.baseColor = {.5f,.5f,.5f,1};
    const auto translucent = [&](float z, bool seeThrough, std::uint32_t alphaMode = 2) {
        BasicDraw draw; draw.mesh = &mesh; draw.model[3].z = z; draw.twoSided = true;
        draw.baseColor = {1,0,0,.6f}; draw.emission = {2,0,0}; draw.alphaMode = alphaMode; draw.seeThrough = seeThrough;
        draw.castsShadow = false; draw.contributesToGi = false;
        return draw;
    };
    const auto red = [&](std::vector<BasicDraw> draws) {
        renderer.Render(glm::mat4(1), lighting, draws, {}, {}, PostProcessDebugView::None, nullptr, nullptr, true);
        const auto pixels = readPixels(renderer.GetColorTexture());
        const auto offset = (std::size_t(.5f * renderer.GetHeight()) * renderer.GetWidth() + std::size_t(.5f * renderer.GetWidth())) * 4;
        return int(pixels.at(offset)) - int(pixels.at(offset + 1));   // Red over green: how red the centre is.
    };
    const int baseline = red({blocker});
    // Which side of the blocker is in front depends on the conventions; exactly one side tints it.
    const int nearSide = red({blocker, translucent(.3f, false)});
    const int farSide = red({blocker, translucent(.7f, false)});
    require((nearSide > baseline + 10) != (farSide > baseline + 10), "Translucent quads on both sides of an opaque one tinted it alike");
    const float behind = nearSide > baseline + 10 ? .7f : .3f;
    const int seen = red({blocker, translucent(behind, true)});
    if (seen <= baseline + 10) std::cout << "see-through: baseline " << baseline << " near " << nearSide << " far " << farSide << " behind " << behind
              << " see-through " << seen << std::endl;
    require(seen > baseline + 10, "See-through translucency was hidden behind an opaque quad");
    require(std::abs(red({blocker, translucent(behind, true, 0)}) - baseline) <= 2,
            "See-through on an opaque material bypassed the depth test");
    std::cout << "See-through translucency checks passed\n";
}
