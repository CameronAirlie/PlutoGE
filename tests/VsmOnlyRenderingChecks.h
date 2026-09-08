#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <array>
#include <algorithm>
#include <stdexcept>
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>

template<class ReadPixels>
void CheckVsmOnlyRendering(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    const auto width = renderer.GetWidth(), height = renderer.GetHeight();
    renderer.Resize(64, 64);
    constexpr std::array<BasicVertex, 4> vertices = {{
        {{{-1,-1,0}}, {{0,0,1}}, {{0,0}}}, {{{1,-1,0}}, {{0,0,1}}, {{1,0}}},
        {{{1,1,0}}, {{0,0,1}}, {{1,1}}}, {{{-1,1,0}}, {{0,0,1}}, {{0,1}}}
    }};
    constexpr std::array<std::uint32_t, 6> indices{0,1,2,0,2,3};
    auto mesh = renderer.CreateMesh({vertices, indices});
    BasicDraw receiver, caster;
    receiver.mesh = caster.mesh = &mesh;
    receiver.model = glm::scale(glm::mat4(1), glm::vec3(4));
    receiver.castsShadow = false;
    caster.model = glm::translate(glm::mat4(1), glm::vec3(0, 0, 2)) *
        glm::scale(glm::mat4(1), glm::vec3(4));
    caster.shadowBoundsCenter = {0, 0, 2};
    caster.shadowBoundsRadius = 6;
    BasicLighting lighting;
    lighting.shadowsEnabled = true;
    lighting.shadowMethod = ShadowMethod::Virtual;
    lighting.shadowResolution = 256;
    lighting.shadowCascadeCount = 4;
    lighting.shadowDistance = 150;
    lighting.shadowCasterDistance = 150;
    lighting.directionalDirection = {0, 0, -1};
    lighting.cameraPosition = {0, 0, 4};
    lighting.view = glm::lookAtRH(lighting.cameraPosition, glm::vec3(0), glm::vec3(0,1,0));
    const auto projection = glm::perspectiveRH_ZO(glm::radians(60.0f), 1.0f, 100.0f, 0.1f);
    const auto assertExclusive = [&]
    {
        const auto &stats = renderer.GetFrameStats();
        if (!stats.virtualShadowsActive || stats.shadowCascadeTargets || stats.shadowCascadeUpdates ||
            stats.shadowCascadeCacheHits || stats.shadowObjectUploads || stats.shadowInstances ||
            std::any_of(stats.shadowDrawsByCascade.begin(), stats.shadowDrawsByCascade.end(), [](auto n) { return n != 0; }))
            throw std::runtime_error("VSM retained CSM targets, updates, cache validation or draws");
    };
    const auto renderSurface = [&]
    {
        renderer.Render(projection * lighting.view, lighting, std::span(&receiver, 1), {}, std::span(&caster, 1),
                        PostProcessDebugView::DirectionalShadowMaskFiltered);
        (void)readPixels(renderer.GetColorTexture());
    };
    for (int frame = 0; frame < 8; ++frame) { renderSurface(); assertExclusive(); }
    const auto shadowed = readPixels(renderer.GetColorTexture());
    std::cout << "VSM surface: requested " << renderer.GetFrameStats().virtualShadows.requested
              << ", hits " << renderer.GetFrameStats().virtualShadows.cacheHits
              << ", center " << int(shadowed[(32 * 64 + 32) * 4]) << '\n';
    caster.castsShadow = false;
    for (int frame = 0; frame < 8; ++frame) { renderSurface(); assertExclusive(); }
    if (shadowed == readPixels(renderer.GetColorTexture()))
        throw std::runtime_error("VSM-only surface shadows disappeared");

    lighting.shadowMethod = ShadowMethod::Cascaded;
    renderSurface();
    if (renderer.GetFrameStats().virtualShadowsActive || renderer.GetFrameStats().shadowCascadeTargets != 4 ||
        renderer.GetFrameStats().shadowCascadeUpdates != 4)
        throw std::runtime_error("Switching to CSM did not allocate and update cascades");
    lighting.shadowMethod = ShadowMethod::Virtual;
    renderSurface(); assertExclusive();

    caster.castsShadow = true;
    for (int frame = 0; frame < 8; ++frame) { renderSurface(); assertExclusive(); }
    lighting.virtualShadowPageBudget = 1;
    bool sawDeferred = false;
    for (int frame = 0; frame < 24; ++frame)
    {
        // Cross fine-page boundaries while dirtying more resident pages than
        // one frame can update. The fully occluded interior must never flash.
        lighting.cameraPosition.x = float(frame) * 0.05f;
        lighting.view = glm::lookAtRH(lighting.cameraPosition,
            glm::vec3(lighting.cameraPosition.x, 0, 0), glm::vec3(0, 1, 0));
        caster.model[3].x += 0.002f;
        renderSurface(); assertExclusive();
        const auto image = readPixels(renderer.GetColorTexture());
        for (int y = 16; y < 48; ++y)
            for (int x = 16; x < 48; ++x)
                if (static_cast<unsigned char>(image[(y * 64 + x) * 4]) > 20)
                    throw std::runtime_error("VSM shadow flashed during movement or a deferred page refresh");
        const auto &stats = renderer.GetFrameStats().virtualShadows;
        sawDeferred |= stats.deferred != 0;
        if (stats.updated > 1 || stats.submittedTriangles > lighting.virtualShadowTriangleBudget)
            throw std::runtime_error("VSM continuity bypassed the update budgets");
    }
    if (!sawDeferred) throw std::runtime_error("VSM continuity test did not exercise deferred refreshes");
    lighting.virtualShadowPageBudget = 64;
    lighting.cameraPosition = {0, 0, 4};
    lighting.view = glm::lookAtRH(lighting.cameraPosition, glm::vec3(0), glm::vec3(0, 1, 0));
    caster.model[3].x = 0;

    // An oblique receiver must sample the fine levels requested for it, rather
    // than falling back to the coarse root in screen-space distance bands.
    renderer.Resize(256, 256);
    receiver.model = glm::scale(glm::mat4(1), glm::vec3(40));
    lighting.shadowDistance = 600;
    lighting.cameraPosition = {0, -4, 2};
    lighting.view = glm::lookAtRH(lighting.cameraPosition, glm::vec3(0), glm::vec3(0, 0, 1));
    for (int frame = 0; frame < 24; ++frame)
        renderer.Render(projection * lighting.view, lighting, std::span(&receiver, 1), {},
                        std::span(&caster, 1), PostProcessDebugView::ShadowCascades);
    assertExclusive();
    const auto oblique = readPixels(renderer.GetColorTexture());
    int rootPixels = 0;
    for (int y = 64; y < 192; ++y)
        for (int x = 64; x < 192; ++x)
        {
            const auto pixel = (y * 256 + x) * 4;
            rootPixels += static_cast<unsigned char>(oblique[pixel]) > 240 &&
                          static_cast<unsigned char>(oblique[pixel + 1]) > 180;
        }
    std::cout << "VSM oblique receiver: " << rootPixels << " unnecessary coarse pixels\n";
    if (rootPixels != 0)
        throw std::runtime_error("VSM requests and shading select different levels on an oblique receiver");
    receiver.model = glm::scale(glm::mat4(1), glm::vec3(4));
    lighting.cameraPosition = {0, 0, 4};
    lighting.view = glm::lookAtRH(lighting.cameraPosition, glm::vec3(0), glm::vec3(0, 1, 0));

    // A grazing light must not make a flat, self-shadowing receiver develop
    // stripes as the projected derivatives shrink across virtual levels.
    renderer.Resize(256, 256);
    receiver.castsShadow = true;
    receiver.shadowBoundsCenter = {0, 0, 0};
    receiver.shadowBoundsRadius = 6;
    lighting.directionalDirection = glm::normalize(glm::vec3(1, 0.2f, -0.15f));
    lighting.shadowSoftness = 4;
    for (float distance : {75.0f, 150.0f, 300.0f})
    {
        lighting.shadowDistance = distance;
        for (int frame = 0; frame < 16; ++frame)
            renderer.Render(projection * lighting.view, lighting, std::span(&receiver, 1), {},
                            std::span(&receiver, 1), PostProcessDebugView::DirectionalShadowMaskFiltered);
        assertExclusive();
        const auto image = readPixels(renderer.GetColorTexture());
        int darkest = 255;
        for (int y = 48; y < 208; ++y)
            for (int x = 48; x < 208; ++x)
                darkest = std::min(darkest, int(static_cast<unsigned char>(image[(y * 256 + x) * 4])));
        std::cout << "VSM grazing receiver at distance " << distance << ": minimum visibility " << darkest << '\n';
        if (darkest < 245)
            throw std::runtime_error("VSM level-dependent self-shadow bands on a flat receiver");
    }
    renderer.Resize(64, 64);
    receiver.castsShadow = false;
    lighting.directionalDirection = {0, 0, -1};
    lighting.shadowDistance = 150;
    lighting.shadowSoftness = 1;

    // Demand substantially more fine pages than fit, then require the stable
    // working set to fit instead of retaining a permanent patchwork of overflow.
    renderer.Resize(256, 256);
    lighting.shadowDistance = 8;
    bool sawOverflow = false;
    for (int frame = 0; frame < 120; ++frame)
    {
        renderSurface(); assertExclusive();
        const auto &stats = renderer.GetFrameStats().virtualShadows;
        sawOverflow |= stats.overflow != 0;
        if (stats.updated > lighting.virtualShadowPageBudget || stats.submittedTriangles > lighting.virtualShadowTriangleBudget)
            throw std::runtime_error("VSM pressure adaptation bypassed update budgets");
    }
    const auto pressure = renderer.GetFrameStats().virtualShadows;
    std::cout << "VSM pressure: " << pressure.requested << " requests, " << pressure.overflow
              << " overflow, resolution scale " << pressure.resolutionScale << '\n';
    if (!sawOverflow || pressure.overflow != 0 || pressure.resolutionScale <= 1)
        throw std::runtime_error("VSM did not fit an overcommitted working set");
    const auto pressureImage = readPixels(renderer.GetColorTexture());
    for (int y = 96; y < 160; ++y)
        for (int x = 96; x < 160; ++x)
            if (static_cast<unsigned char>(pressureImage[(y * 256 + x) * 4]) > 20)
                throw std::runtime_error("VSM pressure adaptation lost shadow coverage");
    renderer.Resize(64, 64);
    lighting.shadowDistance = 150;

    // No opaque receivers: resident coarse VSM pages must also cover fog.
    BasicPostProcessEffect fog{BasicPostProcessEffectType::VolumetricFog};
    fog.quality = 32;
    fog.parameters[0] = {1, 1, 1, 0.2f};
    fog.parameters[1] = {0, 0, 3, 1};
    fog.parameters[2] = {0, 0, 1, 1};
    fog.parameters[3].x = 1;
    const auto renderFog = [&]
    {
        renderer.Render(projection * lighting.view, lighting, {}, std::span(&fog, 1), std::span(&caster, 1));
        (void)readPixels(renderer.GetColorTexture());
        assertExclusive();
    };
    caster.castsShadow = true;
    for (int frame = 0; frame < 20; ++frame) renderFog();
    const auto fogShadowed = readPixels(renderer.GetColorTexture());
    std::cout << "VSM fog: requested " << renderer.GetFrameStats().virtualShadows.requested
              << ", hits " << renderer.GetFrameStats().virtualShadows.cacheHits
              << ", center " << int(fogShadowed[(32 * 64 + 32) * 4]) << '\n';
    if (!renderer.GetFrameStats().virtualShadows.requested)
        throw std::runtime_error("Fog without opaque receivers did not request VSM pages");
    caster.castsShadow = false;
    for (int frame = 0; frame < 20; ++frame) renderFog();
    const auto fogClear = readPixels(renderer.GetColorTexture());
    std::cout << "VSM fog clear: center " << int(fogClear[(32 * 64 + 32) * 4]) << '\n';
    std::uint64_t shadowedEnergy = 0, clearEnergy = 0;
    for (std::size_t pixel = 0; pixel < fogClear.size(); pixel += 4)
    {
        shadowedEnergy += static_cast<unsigned char>(fogShadowed[pixel]);
        clearEnergy += static_cast<unsigned char>(fogClear[pixel]);
    }
    if (clearEnergy <= shadowedEnergy + 64)
        throw std::runtime_error("Fog ignored VSM caster visibility");

    lighting.shadowsEnabled = false;
    renderSurface();
    if (renderer.GetFrameStats().virtualShadowsActive || renderer.GetFrameStats().shadowCascadeTargets)
        throw std::runtime_error("Disabling shadows retained shadow targets");
    renderer.Resize(width, height);
}
