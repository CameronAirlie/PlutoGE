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
    const auto projection = glm::scale(glm::mat4(1), glm::vec3(.1f, .1f, 1));
    const auto render = [&](float camera)
    {
        lighting.cameraPosition.x = camera;
        lighting.view = glm::translate(glm::mat4(1), glm::vec3(-camera, 0, 0));
        for (auto &matrix : lighting.shadowMatrices) matrix = projection * lighting.view;
        renderer.Render(projection * lighting.view, lighting, std::span(&receiver, 1), {}, casters,
                        PostProcessDebugView::DirectionalShadowMaskFiltered);
    };
    for (int frame = 0; frame < 80; ++frame) render(0);
    (void)readPixels(renderer.GetColorTexture());
    for (int scenario = 0; scenario < 3; ++scenario)
    {
        double planning = 0, pages = 0, total = 0;
        std::uint64_t triangles = 0, hits = 0, deferred = 0, commands = 0;
        int samples = 0;
        for (int frame = 0; frame < 20; ++frame)
        {
            if (scenario == 2) casters.front().model[3].x += .01f;
            render(scenario == 0 ? 0.0f : float(frame) * .005f);
            const auto stats = renderer.GetFrameStats().virtualShadows;
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
                if (scope.name == "RHI Virtual Shadow Pages") pages += scope.milliseconds;
            }
            triangles += stats.submittedTriangles; hits += stats.cacheHits; deferred += stats.deferred;
            commands += timing.indexedDrawCalls; ++samples;
        }
        if (samples == 0) throw std::runtime_error("VSM asynchronous GPU statistics never arrived");
        if (scenario == 0 && (triangles != 0 || hits == 0)) throw std::runtime_error("Stationary VSM cache failed to converge");
        std::cout << "VSM performance " << (scenario == 0 ? "stationary" : scenario == 1 ? "camera movement" : "animated caster")
                  << ": GPU frame " << total / samples << " ms, planning " << planning / samples << " ms, pages " << pages / samples
                  << " ms; " << triangles / samples << " triangles, " << hits / samples << " hits, " << deferred / samples
                  << " deferred, " << commands / samples << " total indexed commands\n";
    }
    lighting.shadowMethod = ShadowMethod::Cascaded;
    render(0);
    renderer.Resize(previousWidth, previousHeight);
}
