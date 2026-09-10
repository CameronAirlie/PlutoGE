#pragma once

#include "PlutoGE/render/BasicRenderer.h"
#include <array>
#include <algorithm>
#include <stdexcept>
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>

// Magnify a deliberately coarse shadow map. A nearest-comparison 3x3 PCF
// can produce only ten levels; interpolated coverage must preserve a smooth
// edge even without temporal AA or a screen-space blur.
template <class ReadPixels>
void CheckShadowFiltering(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    const auto width = renderer.GetWidth();
    const auto height = renderer.GetHeight();
    if (!renderer.Resize(256, 256))
        throw std::runtime_error("Shadow filter test resize failed");
    constexpr std::array<BasicVertex, 4> vertices = {{
        {{{-.9f,-.9f,0}}, {{0,0,1}}, {{0,0}}},
        {{{ .9f,-.9f,0}}, {{0,0,1}}, {{1,0}}},
        {{{ .9f, .9f,0}}, {{0,0,1}}, {{1,1}}},
        {{{-.9f, .9f,0}}, {{0,0,1}}, {{0,1}}}
    }};
    constexpr std::array<std::uint32_t, 6> indices{0,1,2,0,2,3};
    auto mesh = renderer.CreateMesh({vertices, indices});
    BasicDraw receiver;
    receiver.mesh = &mesh;
    receiver.twoSided = true;
    receiver.model[3].z = .8f;
    BasicDraw caster = receiver;
    caster.model = glm::translate(glm::mat4(1), glm::vec3(-.45f, 0, .2f)) *
                   glm::rotate(glm::mat4(1), .25f, glm::vec3(0,0,1)) *
                   glm::scale(glm::mat4(1), glm::vec3(.5f, 2, 1));
    BasicLighting lighting;
    lighting.shadowsEnabled = true;
    lighting.shadowCascadeCount = 1;
    lighting.shadowResolution = 256;
    lighting.shadowFilterEnabled = false;
    lighting.shadowSoftness = 1;
    lighting.shadowMatrices[0] = glm::scale(glm::mat4(1), glm::vec3(.1f,.1f,1));
    lighting.directionalDirection = {0,0,-1};
    renderer.Render(glm::mat4(1), lighting, std::span(&receiver, 1), {},
                    std::span(&caster, 1), PostProcessDebugView::DirectionalShadowMaskFiltered);
    const auto pixels = readPixels(renderer.GetColorTexture());
    if (pixels.size() != 256 * 256 * 4)
        throw std::runtime_error("Shadow filter readback size mismatch");
    std::array<bool, 256> levels{};
    // Ignore the receiver boundary and background.
    for (int y = 32; y < 224; ++y)
        for (int x = 32; x < 224; ++x)
            levels[static_cast<unsigned char>(pixels[(y * 256 + x) * 4])] = true;
    const auto intermediateLevels = std::count(levels.begin() + 1, levels.end() - 1, true);
    // The scene's screen-space blur radius must not change the RHI shadow
    // footprint. It previously overrode softness and spread taps four texels
    // apart even with shadow softness set to one.
    lighting.shadowFilterEnabled = true;
    lighting.shadowFilterRadius = 4;
    renderer.Render(glm::mat4(1), lighting, std::span(&receiver, 1), {},
                    std::span(&caster, 1), PostProcessDebugView::DirectionalShadowMaskFiltered);
    if (readPixels(renderer.GetColorTexture()) != pixels)
        throw std::runtime_error("Screen-space filter radius changed shadow-map coverage");
    lighting.shadowFilterEnabled = false;

    // A straight, magnified edge exposes plateaus hidden by counting unique
    // levels across a diagonal. Sparse bilinear taps at radius two create
    // several-pixel constant bands at intermediate visibility.
    caster.model = glm::translate(glm::mat4(1), glm::vec3(-.45f, 0, .2f)) *
                   glm::scale(glm::mat4(1), glm::vec3(.5f, 2, 1));
    lighting.shadowSoftness = 2;
    renderer.Render(glm::mat4(1), lighting, std::span(&receiver, 1), {},
                    std::span(&caster, 1), PostProcessDebugView::DirectionalShadowMaskFiltered);
    const auto edge = readPixels(renderer.GetColorTexture());
    if (edge.size() != 256 * 256 * 4)
        throw std::runtime_error("Shadow edge readback size mismatch");
    int longestPlateau = 0;
    int plateau = 0;
    int previous = -1;
    for (int x = 32; x < 224; ++x)
    {
        const int value = static_cast<unsigned char>(edge[(128 * 256 + x) * 4]);
        plateau = value > 20 && value < 235 && value == previous ? plateau + 1 : 1;
        longestPlateau = std::max(longestPlateau, plateau);
        previous = value;
    }
    std::cout << "Shadow edge: longest intermediate plateau " << longestPlateau << " pixels\n";
    if (longestPlateau > 2)
        throw std::runtime_error("Shadow filtering contains sparse-sample coverage plateaus");
    // GPU VSM uses light direction to construct world-stable clipmaps. The
    // synthetic cascade above supplies its matrix explicitly; match that
    // matrix's +Z shadow direction for the virtual path.
    lighting.directionalDirection = {0, 0, 1};
    lighting.shadowMethod = ShadowMethod::Virtual;
    caster.shadowBoundsCenter = glm::vec3(caster.model[3]);
    caster.shadowBoundsRadius = 2.0f;
    const auto renderVirtual = [&]
    {
        renderer.Render(glm::mat4(1), lighting, std::span(&receiver, 1), {},
                        std::span(&caster, 1), PostProcessDebugView::DirectionalShadowMaskFiltered);
        const auto &frame = renderer.GetFrameStats();
        if (frame.virtualShadowsActive && (frame.shadowCascadeTargets != 0 || frame.shadowCascadeUpdates != 0 ||
            frame.shadowCascadeCacheHits != 0 || frame.shadowObjectUploads != 0 || frame.shadowInstances != 0))
            throw std::runtime_error("VSM performed or retained cascade work");
        return readPixels(renderer.GetColorTexture());
    };
    for (int frame = 0; frame < 8; ++frame) renderVirtual();
    const auto virtualPixels = renderVirtual();
    auto virtualStats = renderer.GetFrameStats().virtualShadows;
    if (!renderer.GetFrameStats().virtualShadowsActive || !virtualStats.gpuCountersAvailable ||
        virtualStats.requested == 0 || virtualStats.cacheHits == 0 || virtualStats.dirty != 0)
        throw std::runtime_error("GPU virtual shadows did not allocate and retain visible pages");
    for (int y = 32; y < 224; ++y)
        if (static_cast<unsigned char>(virtualPixels[(y * 256 + 64) * 4]) > 10 ||
            static_cast<unsigned char>(virtualPixels[(y * 256 + 192) * 4]) < 245)
            throw std::runtime_error("GPU virtual shadow page boundaries lost coverage");
    if (renderVirtual() != virtualPixels)
        throw std::runtime_error("GPU virtual shadow static cache changed pixels");
    for (int frame = 0; frame < 16 && !renderer.GetFrameStats().virtualShadows.reusedFrame; ++frame) renderVirtual();
    const auto idleStats = renderer.GetFrameStats().virtualShadows;
    if (!idleStats.reusedFrame || idleStats.receiverDraws != 0 || idleStats.submittedIndirectCommands != 0)
        throw std::runtime_error("Unchanged clean VSM frame still recorded shadow draws");
    auto instanceModels = std::make_shared<std::vector<glm::mat4>>(65, caster.model);
    caster.instanceModels = instanceModels;
    for (int frame = 0; frame < 8; ++frame) renderVirtual();
    if (renderVirtual() != virtualPixels)
        throw std::runtime_error("Mixed rigid/64-instance shadow chunks changed coverage");
    instanceModels->back()[3].x += .4f;
    if (renderVirtual() == virtualPixels || renderer.GetFrameStats().virtualShadows.reusedFrame)
        throw std::runtime_error("Last instance edit reused stale shadow inputs");
    caster.instanceModels.reset();
    for (int frame = 0; frame < 8; ++frame) renderVirtual();
    // Moving the clipmap window by one page changes local addresses while
    // preserving the depth identity of still-visible absolute world pages.
    lighting.cameraPosition.x += VirtualShadowMaps::BuildClipmaps(lighting).metrics[1].z;
    for (int frame = 0; frame < 5; ++frame) renderVirtual();
    if (renderer.GetFrameStats().virtualShadows.cacheHits == 0)
        throw std::runtime_error("Clipmap scrolling discarded all world-stable cached pages");
    caster.model[3].x += 0.4f;
    caster.shadowBoundsCenter = glm::vec3(caster.model[3]);
    if (renderVirtual() == virtualPixels)
        throw std::runtime_error("GPU caster movement did not invalidate old/new pages");
    caster.castsShadow = false;
    auto removed = renderVirtual();
    if (static_cast<unsigned char>(removed[(128 * 256 + 64) * 4]) < 245)
        throw std::runtime_error("GPU removed caster retained stale depth");
    caster.castsShadow = true;
    caster.alphaMode = 1; caster.baseColor.a = 0;
    removed = renderVirtual();
    if (static_cast<unsigned char>(removed[(128 * 256 + 64) * 4]) < 245)
        throw std::runtime_error("GPU alpha mask did not discard transparent texels");
    caster.alphaMode = 0; caster.baseColor.a = 1;
    caster.model[3].x -= 0.4f;
    caster.shadowBoundsCenter = glm::vec3(caster.model[3]);
    lighting.virtualShadowPageBudget = 1;
    lighting.virtualShadowTriangleBudget = 1; // A two-triangle caster cannot fit.
    lighting.directionalDirection = {0.02f, 0, 1};
    for (int frame = 0; frame < 6; ++frame) renderVirtual();
    virtualStats = renderer.GetFrameStats().virtualShadows;
    if (virtualStats.updated > 1 || virtualStats.submittedTriangles > 1 || virtualStats.deferred == 0)
        throw std::runtime_error("GPU virtual shadow update budgets were exceeded or did not defer work");
    const auto fallbackPixels = renderVirtual();
    if (static_cast<unsigned char>(fallbackPixels[(128 * 256 + 64) * 4]) < 245)
        throw std::runtime_error("Budget-deferred VSM pages sampled stale or conventional shadow depth");
    lighting.shadowMethod = ShadowMethod::Cascaded;
    lighting.directionalDirection = {0, 0, -1};
    if (renderVirtual() != edge || renderer.GetFrameStats().virtualShadowsActive)
        throw std::runtime_error("Switching to cascades changed their original output");
    lighting.shadowMethod = ShadowMethod::Virtual;
    lighting.directionalDirection = {0, 0, 1};
    lighting.virtualShadowPageBudget = 64; lighting.virtualShadowTriangleBudget = 1000000;
    for (int frame = 0; frame < 6; ++frame) renderVirtual();
    lighting.shadowsEnabled = false;
    renderVirtual();
    if (renderer.GetFrameStats().virtualShadowsActive)
        throw std::runtime_error("Disabled shadows retained the GPU virtual path");
    std::cout << "GPU virtual shadows: depth requests, residency, scrolling, invalidation, masks, budgets and switching passed\n";
    if (!renderer.Resize(width, height))
        throw std::runtime_error("Shadow filter test restore failed");
    std::cout << "Shadow filter: " << intermediateLevels << " intermediate coverage levels\n";
    if (!levels[0] || !levels[255] || intermediateLevels < 16)
        throw std::runtime_error("Shadow edge lacks smooth sub-texel comparison coverage");
}
