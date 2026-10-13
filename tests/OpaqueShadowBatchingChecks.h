#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

template <class ReadPixels>
void CheckOpaqueShadowBatching(PlutoGE::render::BasicRenderer &renderer,
                              PlutoGE::render::rhi::IRenderDevice &device, ReadPixels readPixels, bool requireGpuTiming = true)
{
    using namespace PlutoGE::render;
    const auto require = [](bool value, const char *message) { if (!value) throw std::runtime_error(message); };
    require(renderer.Resize(128, 96), "Opaque shadow fixture resize failed");
    std::vector<BasicVertex> vertices;
    std::vector<std::uint32_t> indices;
    constexpr unsigned divisions = 16;
    for (unsigned y = 0; y <= divisions; ++y)
        for (unsigned x = 0; x <= divisions; ++x)
        {
            BasicVertex vertex{};
            vertex.position = {float(x) / divisions * .7f - .35f, float(y) / divisions * .7f - .35f, 0};
            vertex.normal = {0, 0, 1}; vertex.uv = {float(x) / divisions, float(y) / divisions};
            vertices.push_back(vertex);
        }
    for (unsigned y = 0; y < divisions; ++y)
        for (unsigned x = 0; x < divisions; ++x)
        {
            const auto a = y * (divisions + 1) + x, b = a + divisions + 1;
            indices.insert(indices.end(), {a, a + 1, b, b, a + 1, b + 1});
        }
    auto mesh = renderer.CreateMesh({vertices, indices});
    std::vector<BasicDraw> casters(649);
    for (std::size_t i = 0; i < casters.size(); ++i)
    {
        auto &draw = casters[i]; draw.mesh = &mesh;
        draw.model = glm::translate(glm::mat4(1), glm::vec3(float(i % 31) - 15, float(i / 31) - 10, .2f));
        draw.baseColor = {float(i % 7) / 7, float(i % 11) / 11, float(i % 13) / 13, 1};
    }
    BasicDraw receiver;
    receiver.mesh = &mesh; receiver.castsShadow = false;
    receiver.model = glm::translate(glm::mat4(1), glm::vec3(0, 0, .8f)) * glm::scale(glm::mat4(1), glm::vec3(60, 60, 1));
    BasicLighting lighting;
    lighting.shadowsEnabled = true; lighting.shadowMethod = ShadowMethod::Cascaded;
    lighting.shadowCascadeCount = 1; lighting.shadowResolution = 512;
    lighting.shadowSoftness = 2; lighting.directionalDirection = {0, 0, -1};
    lighting.view = glm::scale(glm::mat4(1), glm::vec3(1, 1, -1));
    lighting.shadowCascadeSplits = {1, 2, 3, 10};
    for (unsigned cascade = 0; cascade < 4; ++cascade)
        lighting.shadowMatrices[cascade] = glm::scale(glm::mat4(1), glm::vec3(.05f, .05f, 1)) *
            glm::translate(glm::mat4(1), glm::vec3(float(cascade), 0, 0));
    const auto projection = glm::scale(glm::mat4(1), glm::vec3(.05f, .05f, 1));
    const auto render = [&] {
        renderer.Render(projection, lighting, std::span(&receiver, 1), {}, casters,
                        PostProcessDebugView::DirectionalShadowMaskFiltered);
    };
    const auto compare = [&] {
        renderer.SetOpaqueShadowBatchingEnabled(false); renderer.SetStaticShadowCachingEnabled(false); render();
        const auto expected = readPixels(renderer.GetColorTexture());
        const auto oldInstances = renderer.GetFrameStats().shadowInstances;
        renderer.SetOpaqueShadowBatchingEnabled(true); renderer.SetStaticShadowCachingEnabled(true); render();
        const auto actual = readPixels(renderer.GetColorTexture());
        require(actual.size() == 128 * 96 * 4 && actual.size() == expected.size(), "Opaque shadow readback mismatch");
        for (std::size_t i = 0; i < actual.size(); ++i)
            require(std::abs(int(actual[i]) - int(expected[i])) <= 1, "Opaque shadow batching changed coverage");
        require(renderer.GetFrameStats().shadowInstances == oldInstances, "Opaque shadow batching lost caster instances");
    };
    compare();
    require(renderer.GetFrameStats().ShadowDraws() == 11 && renderer.GetFrameStats().shadowObjectUploads == 11,
            "649 opaque casters were not reduced to eleven material-free batches");
    const auto cached = readPixels(renderer.GetColorTexture());
    render();
    require(renderer.GetFrameStats().ShadowDraws() == 0 && renderer.GetFrameStats().shadowCascadeCacheHits == 1,
            "Opaque batching invalidated an unchanged shadow cache");
    require(readPixels(renderer.GetColorTexture()) == cached, "Opaque batching changed a cached image");
    casters[0].alphaMode = 1; casters[0].baseColor.a = 0;
    ShaderGraph graph;
    graph.nodes = {{.id = 2, .kind = ShaderGraphNodeKind::Vec3, .value = {.2f, 0, 0, 1}},
                   {.id = 100, .kind = ShaderGraphNodeKind::Output}};
    graph.links = {{1, 2, "Vec3", 100, "Vertex Offset"}};
    casters[1].shaderGraphProgram = BuildShaderGraphProgram(graph);
    require(casters[1].shaderGraphProgram && casters[1].shaderGraphProgram->data.header.z > 0, "Shadow vertex graph fixture invalid");
    auto models = std::make_shared<std::vector<glm::mat4>>(65, casters[2].model);
    for (std::size_t i = 0; i < models->size(); ++i) (*models)[i][3].x += .01f * i;
    casters[2].instanceModels = models;
    compare();
    models->back()[3].x += 1; // Mutation behind the same shared_ptr must remain visible.
    casters[3].model = casters[3].model * glm::scale(glm::mat4(1), glm::vec3(-1, 1, 1));
    compare();
    lighting.shadowCascadeCount = 4;
    lighting.shadowCascadeSplits = {.1f, .2f, .4f, 10}; // Receiver samples cascade 3.
    compare();
    casters[4].castsShadow = false;
    casters[5].shadowBoundsCenter = {1000, 1000, 0}; casters[5].shadowBoundsRadius = .1f;
    compare();
    // Exercise reused static depth, not just the rebuild path used by compare.
    // Leave masks, graph deformation, instancing and cascade 3 active here.
    renderer.SetOpaqueShadowBatchingEnabled(true); renderer.SetStaticShadowCachingEnabled(true);
    for (unsigned frame = 0; frame < 5; ++frame)
    {
        for (unsigned actor = 0; actor < 9; ++actor) casters[actor].model[3].y += .01f;
        render();
    }
    const auto reusedLayer = readPixels(renderer.GetColorTexture());
    renderer.SetOpaqueShadowBatchingEnabled(false); renderer.SetStaticShadowCachingEnabled(false); render();
    const auto completeLayer = readPixels(renderer.GetColorTexture());
    for (std::size_t i = 0; i < reusedLayer.size(); ++i)
        require(std::abs(int(reusedLayer[i]) - int(completeLayer[i])) <= 1, "Cached static shadow layer changed coverage");

    // Measure rebuilding, not the stationary-cache fast path: nine moving
    // casters invalidate every frame while the other 640 remain fixed.
    casters[0].alphaMode = 0; casters[0].baseColor.a = 1;
    casters[1].shaderGraphProgram.reset(); casters[2].instanceModels.reset();
    casters[3].model = glm::translate(glm::mat4(1), glm::vec3(-12, -10, .2f));
    casters[4].castsShadow = true; casters[5].shadowBoundsRadius = -1;
    lighting.shadowCascadeCount = 1; lighting.shadowCascadeSplits = {1, 2, 3, 10};
    lighting.shadowResolution = 2048; // Match the capture's map resolution.
    device.GetImmediateContext().SetGpuProfilingEnabled(true);
    std::array<double, 2> cpu{}, gpu{};
    std::array<unsigned, 2> cpuSamples{}, gpuSamples{};
    for (bool batched : {false, true, true, false})
    {
        renderer.SetOpaqueShadowBatchingEnabled(batched);
        renderer.SetStaticShadowCachingEnabled(false);
        std::uint64_t previousObservation = 0;
        for (unsigned frame = 0; frame < 96; ++frame)
        {
            for (unsigned actor = 0; actor < 9; ++actor)
                casters[actor].model[3].x = float(actor) - 15 + .1f * std::sin(float(frame) * .13f + actor);
            render();
            if (frame < 16) continue;
            const auto &stats = renderer.GetFrameStats();
            require(stats.shadowInstances == 649u && stats.ShadowDraws() == (batched ? 11u : 649u),
                    "Dynamic opaque shadow workload changed or stopped rebuilding");
            cpu[batched] += renderer.GetTimingStats().shadowRecordingMs;
            ++cpuSamples[batched];
            const auto timing = device.GetTimingStats("Scene");
            if (timing.hasGpuResult && timing.gpuObservationId != 0 && timing.gpuObservationId != previousObservation)
                for (const auto &scope : timing.gpuScopes)
                    if (scope.name == "RHI Shadow Cascade 0") { gpu[batched] += scope.milliseconds; ++gpuSamples[batched]; }
            previousObservation = timing.gpuObservationId;
        }
    }
    if (requireGpuTiming) require(gpuSamples[0] && gpuSamples[1], "Opaque shadow GPU observations unavailable");
    std::cout << "Opaque shadow ABBA: legacy CPU " << cpu[0] / cpuSamples[0]
              << " ms, batched CPU " << cpu[1] / cpuSamples[1]
              << " ms";
    if (gpuSamples[0] && gpuSamples[1])
        std::cout << "; legacy GPU " << gpu[0] / gpuSamples[0]
                  << " ms, batched GPU " << gpu[1] / gpuSamples[1] << " ms";
    else std::cout << "; GPU timing unavailable on this backend";
    std::cout << "; draws 649 -> 11, rendered casters 649, moving casters 9\n";
}
