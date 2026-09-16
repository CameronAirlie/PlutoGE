#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
#include <string>

template<class ReadPixels>
void CheckOcclusionRendering(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    const std::array<BasicVertex, 4> vertices{{
        {{{-1,-1,0}}, {{0,0,1}}, {{0,0}}}, {{{1,-1,0}}, {{0,0,1}}, {{1,0}}},
        {{{1,1,0}}, {{0,0,1}}, {{1,1}}}, {{{-1,1,0}}, {{0,0,1}}, {{0,1}}}
    }};
    const std::array<std::uint32_t, 6> indices{0,1,2,0,2,3};
    auto mesh = renderer.CreateMesh({vertices, indices});
    BasicDraw wall; wall.mesh = &mesh; wall.emission = {.2f,.2f,.2f};
    wall.model[0][0] = wall.model[1][1] = .65f; wall.model[3].z = .8f;
    wall.shadowBoundsCenter = {0,0,.8f}; wall.shadowBoundsRadius = 1;
    BasicDraw hidden = wall; hidden.emission = {1,0,0};
    hidden.model[0][0] = hidden.model[1][1] = .08f; hidden.model[3].z = .3f;
    hidden.shadowBoundsCenter = {0,0,.3f}; hidden.shadowBoundsRadius = .12f;
    BasicDraw visible = hidden; visible.emission = {0,1,0};
    visible.model[3].x = .85f; visible.shadowBoundsCenter.x = .85f;
    BasicLighting lighting; lighting.shadowsEnabled = false;
    lighting.ambientIntensity = lighting.directionalIntensity = 0;
    bool jitter = false;
    const auto render = [&](std::span<const BasicDraw> draws, OcclusionMode mode, glm::mat4 camera = glm::mat4(1))
    {
        lighting.occlusionMode = mode;
        std::vector<std::byte> pixels;
        for (int frame = 0; frame < 5; ++frame)
        {
            rhi::TemporalUpscalerFrame temporal;
            temporal.jitterPixels = {.4f, -.3f};
            renderer.Render(camera, lighting, draws, {}, {}, PostProcessDebugView::None, jitter ? &temporal : nullptr);
            auto current = readPixels(renderer.GetColorTexture());
            if (frame > 0 && current != pixels)
                throw std::runtime_error("Occlusion changed visibility after the first stationary frame");
            pixels = std::move(current);
        }
        return pixels;
    };
    for (auto size : {rhi::Extent2D{127,73}, rhi::Extent2D{33,17}})
    {
        renderer.Resize(size.width, size.height);
        for (int scenario = 0; scenario < 12; ++scenario)
        {
            jitter = scenario == 6;
            lighting.shadowsEnabled = scenario == 7;
            lighting.shadowMethod = ShadowMethod::Virtual;
            std::array draws{hidden, visible, wall};
            glm::mat4 camera(1);
            if (scenario == 1) { draws[2].model[3].x = 2; draws[2].shadowBoundsCenter.x = 2; }
            if (scenario == 2) draws[0].shadowBoundsRadius = -1; // unknown bounds fail open
            if (scenario == 3) { draws[2].alphaMode = 1; draws[2].baseColor.a = 0; }
            if (scenario == 4) { camera[3].y = .45f; } // asymmetric camera translation / Vulkan Y
            if (scenario == 5) { draws[0].shadowBoundsRadius = 1; } // near-plane bounds fail open
            if (scenario == 8) draws[0].shadowBoundsRadius = std::numeric_limits<float>::quiet_NaN();
            if (scenario == 9) { draws[0].model[3].x = .6f; draws[0].shadowBoundsCenter.x = .6f; }
            if (scenario == 10)
                draws[0].instanceModels = std::make_shared<const std::vector<glm::mat4>>(std::vector<glm::mat4>{hidden.model, hidden.model});
            if (scenario == 11) draws[0].shaderGraphProgram = std::make_shared<ShaderGraphProgram>();
            const auto reference = render(draws, OcclusionMode::Off, camera);
            const auto measured = render(draws, OcclusionMode::Measure, camera);
            const auto culled = render(draws, OcclusionMode::Cull, camera);
            if (reference != measured || reference != culled)
                throw std::runtime_error("Occlusion changed rendered pixels in scenario " + std::to_string(scenario));
            const auto stats = renderer.GetFrameStats();
            if (!stats.occlusionActive || !stats.occlusion.available)
                throw std::runtime_error("Occlusion GPU statistics unavailable");
            if (scenario == 7 && !stats.virtualShadowsActive) throw std::runtime_error("VSM reuse test inactive");
            if (stats.occlusion.tested + stats.occlusion.unsupported + stats.occlusion.invalidBounds + stats.occlusion.clipped != draws.size())
                throw std::runtime_error("Occlusion exclusion counters do not account for every draw");
            if ((scenario == 2 || scenario == 8) && stats.occlusion.invalidBounds != 1)
                throw std::runtime_error("Occlusion failed to diagnose invalid bounds");
            if (scenario == 10 && stats.occlusion.unsupported != 1)
                throw std::runtime_error("Occlusion failed to diagnose instance fallback");
            const auto expected = scenario == 0 || scenario == 4 || scenario == 6 || scenario == 7 || scenario == 11 ? 1u : 0u;
            if (stats.occlusion.rejected != expected)
                throw std::runtime_error("Unexpected hidden draw count in scenario " + std::to_string(scenario) +
                                         ": " + std::to_string(stats.occlusion.rejected));
        }
    }
    // Thin meshes used to inherit a sphere's excessive depth/screen extent.
    renderer.Resize(127, 73);
    lighting.shadowsEnabled = false;
    jitter = false;
    std::array precise{hidden, visible, wall};
    precise[0].model[0][0] = -.4f; // negative/nonuniform scale must remain conservative
    precise[0].model[1][1] = .025f;
    precise[0].shadowBoundsRadius = .41f;
    precise[2].model[1][1] = .16f;
    const auto thinReference = render(precise, OcclusionMode::Off);
    render(precise, OcclusionMode::Measure);
    if (renderer.GetFrameStats().occlusion.rejected != 0)
        throw std::runtime_error("Thin-mesh baseline unexpectedly rejected loose bounds");
    OcclusionCulling::SetRigidBounds(precise[0], {-1,-1,0}, {1,1,0});
    const auto thinCulled = render(precise, OcclusionMode::Cull);
    if (thinCulled != thinReference || renderer.GetFrameStats().occlusion.rejected != 1 ||
        renderer.GetFrameStats().occlusion.tightBounds != 1)
        throw std::runtime_error("Tight rigid bounds failed to recover hidden thin geometry");

    // Coarse hierarchy cells overlap sky outside the occludee's rectangle.
    // Finer cells prove it hidden without changing any depth comparison bias.
    std::array refinement{hidden, visible, wall};
    refinement[0].model[3].x = refinement[0].shadowBoundsCenter.x = .25f;
    refinement[2].model[3].x = refinement[2].shadowBoundsCenter.x = .25f;
    refinement[2].model[0][0] = .22f;
    refinement[2].model[1][1] = .28f;
    const auto refineReference = render(refinement, OcclusionMode::Off);
    const auto refineCulled = render(refinement, OcclusionMode::Cull);
    if (refineReference != refineCulled || renderer.GetFrameStats().occlusion.refinementRejected != 1)
        throw std::runtime_error("Fine depth testing did not recover coarse-cell false visibility");
    std::array exposed{hidden, visible};
    exposed[0].shadowBoundsRadius = .25f;
    const auto exposedReference = render(exposed, OcclusionMode::Off);
    if (render(exposed, OcclusionMode::Cull) != exposedReference ||
        renderer.GetFrameStats().occlusion.rejected != 0 || renderer.GetFrameStats().occlusion.budgetExceeded == 0)
        throw std::runtime_error("Refinement budget exhaustion did not preserve visible geometry");

    // Perspective projection must conservatively bound changing clip W too.
    std::array perspectiveDraws{hidden, visible, wall};
    perspectiveDraws[0].model[3].z = perspectiveDraws[0].shadowBoundsCenter.z = -3;
    perspectiveDraws[1].model[3].z = perspectiveDraws[1].shadowBoundsCenter.z = -3;
    perspectiveDraws[1].model[3].x = perspectiveDraws[1].shadowBoundsCenter.x = 3;
    perspectiveDraws[2].model[3].z = perspectiveDraws[2].shadowBoundsCenter.z = -1;
    glm::mat4 perspective(0);
    perspective[0][0] = perspective[1][1] = 1;
    perspective[2][2] = .1f / 99.9f; perspective[2][3] = -1;
    perspective[3][2] = 10.0f / 99.9f;
    const auto perspectiveReference = render(perspectiveDraws, OcclusionMode::Off, perspective);
    if (render(perspectiveDraws, OcclusionMode::Cull, perspective) != perspectiveReference ||
        renderer.GetFrameStats().occlusion.rejected != 1)
        throw std::runtime_error("Perspective occlusion changed pixels or missed the hidden draw");
    std::cout << "Occlusion precision: tighter rigid bounds and finer depth recovered hidden draws with identical pixels\n";
    std::cout << "Occlusion: Off/Measure/Cull pixel parity, hidden draws, camera motion/jitter, VSM reuse, masked coverage, partial exposure, instance fallback, invalid/near-plane bounds and odd extents passed\n";
}
