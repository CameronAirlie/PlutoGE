#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>
#include <string_view>

// Deliberately pessimistic unknown caster bounds reproduce the original
// many-pages-per-mesh risk. The GPU must bound actual work, not merely CPU calls.
template<class ReadPixels>
void CheckVirtualShadowPerformance(PlutoGE::render::BasicRenderer &renderer,
                                  PlutoGE::render::rhi::IRenderDevice &device, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    const auto previousWidth = renderer.GetWidth(), previousHeight = renderer.GetHeight();
    renderer.Resize(582, 507);
    std::vector<BasicVertex> vertices;
    std::vector<std::uint32_t> indices;
    constexpr int divisions = 40;
    for (int y = 0; y <= divisions; ++y)
        for (int x = 0; x <= divisions; ++x)
        {
            BasicVertex vertex{};
            vertex.position = {float(x) / divisions - 0.5f, float(y) / divisions - 0.5f, 0};
            vertex.normal = {0, 0, 1}; vertex.uv = {float(x) / divisions, float(y) / divisions};
            vertices.push_back(vertex);
        }
    for (int y = 0; y < divisions; ++y)
        for (int x = 0; x < divisions; ++x)
        {
            const auto a = std::uint32_t(y * (divisions + 1) + x), b = a + divisions + 1;
            indices.insert(indices.end(), {a, a + 1, b + 1, a, b + 1, b});
        }
    auto mesh = renderer.CreateMesh({vertices, indices});
    BasicDraw receiver;
    receiver.mesh = &mesh;
    receiver.model = glm::translate(glm::mat4(1), glm::vec3(0, 0, .8f)) * glm::scale(glm::mat4(1), glm::vec3(18, 18, 1));
    receiver.castsShadow = false;
    std::vector<BasicDraw> casters(120);
    for (std::size_t index = 0; index < casters.size(); ++index)
    {
        auto &draw = casters[index]; draw.mesh = &mesh;
        draw.model = glm::translate(glm::mat4(1), glm::vec3(float(index % 12) - 5.5f, float(index / 12) - 4.5f, .2f)) *
                     glm::scale(glm::mat4(1), glm::vec3(.2f, .2f, 1));
        draw.shadowBoundsRadius = -1;
    }
    BasicLighting lighting;
    lighting.shadowsEnabled = true; lighting.shadowMethod = ShadowMethod::Virtual;
    lighting.shadowCascadeCount = 4; lighting.shadowResolution = 256;
    lighting.shadowDistance = 150; lighting.directionalDirection = {0, 0, 1};
    lighting.shadowCascadeSplits = glm::vec4(150);
    lighting.shadowSoftness = 1;
    auto diagnosticView = PostProcessDebugView::DirectionalShadowMaskFiltered;
    const auto projection = glm::scale(glm::mat4(1), glm::vec3(.1f, .1f, 1));
    std::vector<BasicDraw> receivers{receiver};
    const auto render = [&](float camera)
    {
        lighting.cameraPosition.x = camera;
        lighting.view = glm::translate(glm::mat4(1), glm::vec3(-camera, 0, 0));
        for (auto &matrix : lighting.shadowMatrices) matrix = projection * lighting.view;
        renderer.Render(projection * lighting.view, lighting, receivers, {}, casters,
                        diagnosticView);
    };
    for (int frame = 0; frame < 80; ++frame) render(0);
    (void)readPixels(renderer.GetColorTexture());
    for (int scenario = 0; scenario < 3; ++scenario)
    {
        double planning = 0, pages = 0, total = 0, receiverRequests = 0;
        std::uint64_t triangles = 0, hits = 0, deferred = 0, commands = 0;
        int samples = 0;
        for (int frame = 0; frame < 20; ++frame)
        {
            if (scenario == 2) casters.front().model[3].x += .01f;
            render(scenario == 0 ? 0.0f : float(frame) * .005f);
            const auto stats = renderer.GetFrameStats().virtualShadows;
            if (scenario > 0 && frame > 0 && stats.reusedFrame)
                throw std::runtime_error("Changing VSM inputs reused a stale completed frame");
            if (!renderer.GetFrameStats().virtualShadowsActive) throw std::runtime_error("GPU VSM performance path unavailable");
            const auto &frameStats = renderer.GetFrameStats();
            if (frameStats.shadowCascadeTargets || frameStats.shadowCascadeUpdates || frameStats.shadowCascadeCacheHits ||
                frameStats.shadowObjectUploads || frameStats.shadowInstances)
                throw std::runtime_error("VSM performance path retained cascade resources or work");
            if (stats.submittedTriangles > lighting.virtualShadowTriangleBudget || stats.updated > lighting.virtualShadowPageBudget ||
                stats.submittedIndirectCommands > casters.size())
                throw std::runtime_error("VSM performance budget regression");
            if (frame < 5 || !stats.gpuCountersAvailable) continue;
            const auto timing = device.GetTimingStats("Scene");
            if (timing.indexedDrawCalls > 122) throw std::runtime_error("VSM recorded non-VSM shadow draws");
            total += timing.frameGpuMs;
            for (const auto &scope : timing.gpuScopes)
            {
                if (scope.name.starts_with("RHI Shadow Cascade"))
                    throw std::runtime_error("VSM submitted a cascade GPU scope");
                if (scope.name == "RHI VSM GPU Planning") planning += scope.milliseconds;
                if (scope.name == "RHI VSM Planning / Receiver requests") receiverRequests += scope.milliseconds;
                if (scope.name == "RHI Virtual Shadow Pages") pages += scope.milliseconds;
            }
            triangles += stats.submittedTriangles; hits += stats.cacheHits; deferred += stats.deferred;
            commands += timing.indexedDrawCalls; ++samples;
        }
        if (samples == 0) throw std::runtime_error("VSM asynchronous GPU statistics never arrived");
        if (scenario == 0 && commands / samples > 2)
            throw std::runtime_error("Stationary VSM frame retained redundant draw recording");
        if (scenario == 0 && (triangles != 0 || hits == 0)) throw std::runtime_error("Stationary VSM cache failed to converge");
        std::cout << "VSM performance " << (scenario == 0 ? "stationary" : scenario == 1 ? "camera movement" : "animated caster")
                  << ": GPU frame " << total / samples << " ms, planning " << planning / samples << " ms, pages " << pages / samples
                  << " ms, receiver requests " << receiverRequests / samples << " ms; " << triangles / samples << " triangles, " << hits / samples << " hits, " << deferred / samples
                  << " deferred, " << commands / samples << " total indexed commands\n";
    }
    // Isolate receiver shading from page creation using a converged static cache.
    // These diagnostic variants are benchmark inputs, not production quality settings.
    for (const auto extent : {rhi::Extent2D{603, 346}, rhi::Extent2D{1005, 594}})
    {
        renderer.Resize(extent.width, extent.height);
        for (int variant = 0; variant < 6; ++variant)
        {
            lighting.shadowsEnabled = variant != 0 && variant != 3;
            lighting.shadowSoftness = variant == 2 || variant >= 4 ? 1.0f : 0.0f;
            lighting.physicalSkyEnabled = variant >= 3;
            diagnosticView = variant == 5 ? PostProcessDebugView::None : PostProcessDebugView::DirectionalShadowMaskFiltered;
            lighting.physicalSkyParameters[0] = {0.508215f, 0.802401f, 0.312843f, 1};
            lighting.physicalSkyParameters[1] = {1, .98f, .95f, .65f};
            lighting.physicalSkyParameters[2] = {.55f, .65f, .95f, .65f};
            lighting.physicalSkyParameters[3] = {.012f, .015f, .02f, 1};
            lighting.physicalSkyParameters[4] = {8, .27f, .035f, 0};
            lighting.physicalSkyParameters[5] = {.8f, .26f, 0, 0};
            for (int frame = 0; frame < 64; ++frame) render(0);
            double geometry = 0, total = 0;
            int samples = 0;
            for (int frame = 0; frame < 64; ++frame)
            {
                render(0);
                const auto timing = device.GetTimingStats("Scene");
                if (!timing.hasGpuResult) continue;
                for (const auto &scope : timing.gpuScopes)
                    if (scope.name == "RHI Geometry")
                    {
                        geometry += scope.milliseconds;
                        total += timing.frameGpuMs;
                        ++samples;
                    }
            }
            if (!samples) throw std::runtime_error("Geometry diagnostic has no GPU samples");
            std::cout << "Geometry diagnostic " << extent.width << 'x' << extent.height << ' '
                      << (variant == 0 ? "no sky/shadows" : variant == 1 ? "VSM softness 0" :
                          variant == 2 ? "VSM softness 1" : variant == 3 ? "physical sky only" :
                          variant == 4 ? "physical sky + VSM softness 1" : "physical sky + VSM without diagnostic output")
                      << ": geometry " << geometry / samples << " ms, scene " << total / samples
                      << " ms (" << samples << " samples)\n";
        }
    }
    // Paired runtime modes share the same shader, scene and warmed resources.
    // Reverse mode order on the second run to expose order/clock effects.
    lighting.shadowsEnabled = true;
    lighting.physicalSkyEnabled = true;
    diagnosticView = PostProcessDebugView::None;
    std::array<std::byte, 8 * 8 * 4> alphaPixels;
    alphaPixels.fill(std::byte{255});
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            alphaPixels[(y * 8 + x) * 4 + 3] = std::byte{static_cast<unsigned char>((x + y) % 2 ? 255 : 0)};
    rhi::Texture alphaTexture(device, device.CreateTexture(
        {8, 8, rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::Sampled, "Gather benchmark alpha mask"}, alphaPixels));
    for (int scenario = 0; scenario < 2; ++scenario)
    {
        receivers.assign(scenario ? 8 : 1, receiver);
        for (std::size_t i = 0; i < receivers.size(); ++i)
            if (scenario)
            {
                receivers[i].model = glm::translate(glm::mat4(1), glm::vec3(float(i) * .07f, 0, .5f + .01f * i)) *
                    glm::rotate(glm::mat4(1), .04f * float(i + 1), glm::vec3(0, 1, 0)) *
                    glm::scale(glm::mat4(1), glm::vec3(18, 18, 1));
                if (i % 2)
                {
                    receivers[i].baseColorTexture = alphaTexture.Get();
                    receivers[i].alphaMode = 1;
                    receivers[i].alphaCutoff = .5f;
                }
            }
        for (float softness : {0.0f, 0.5f, 1.0f, 4.0f})
            for (int run = 0; run < 2; ++run)
                for (int order = 0; order < 2; ++order)
                {
                    const bool reference = (order ^ run) == 0;
                    lighting.geometryDiagnosticMode = reference ? GeometryDiagnosticMode::ReferenceDirectionalShadows : GeometryDiagnosticMode::None;
                    lighting.shadowSoftness = softness;
                    double geometry = 0;
                    int samples = 0;
                    for (int frame = 0; frame < 96; ++frame)
                    {
                        if (scenario) casters.front().model[3].x = -5.5f + float(frame) * .001f;
                        render(scenario ? float(frame % 16) * .003f : 0);
                        if (frame < 64) continue;
                        const auto timing = device.GetTimingStats("Scene");
                        if (!timing.hasGpuResult) continue;
                        for (const auto &scope : timing.gpuScopes)
                            if (scope.name == "RHI Geometry") { geometry += scope.milliseconds; ++samples; }
                    }
                    if (!samples) throw std::runtime_error("Gather comparison has no GPU samples");
                    std::cout << "Gather paired scenario " << scenario << " softness " << softness << " run " << run
                              << (reference ? " scalar " : " gather ") << geometry / samples << " ms\n";
                }
    }
    lighting.geometryDiagnosticMode = GeometryDiagnosticMode::None;
    lighting.shadowsEnabled = true;
    lighting.physicalSkyEnabled = false;
    lighting.shadowSoftness = 1;
    lighting.shadowMethod = ShadowMethod::Cascaded;
    render(0);
    renderer.Resize(previousWidth, previousHeight);
}
