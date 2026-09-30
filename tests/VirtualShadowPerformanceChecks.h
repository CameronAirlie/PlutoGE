#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <string_view>

// Unknown authored bounds exercise automatic cluster bounds. The GPU must
// bound actual work and submit one command per cluster, never per page pair.
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
    // Include a larger mesh so the image comparison covers split clusters as
    // well as the page-sized meshes that should coalesce to single submissions.
    casters.back().model = glm::translate(glm::mat4(1), glm::vec3(6,5,.2f)) * glm::scale(glm::mat4(1), glm::vec3(8,8,1));
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
    // Compare identical geometry with and without clustered page culling. Warm
    // both paths fully before comparing images; time a moving caster separately.
    std::array<double, 2> comparisonGpu{}, comparisonCpu{};
    std::array<std::uint64_t, 2> comparisonTriangles{};
    std::vector<std::byte> referenceImage;
    const float originalX = casters.front().model[3].x;
    lighting.virtualShadowCoarseMinCasterTexels = 0;
    for (int clustered = 0; clustered < 2; ++clustered)
    {
        lighting.virtualShadowClusterCulling = clustered != 0;
        casters.front().model[3].x = originalX;
        for (int frame = 0; frame < 160; ++frame) render(0);
        const auto image = readPixels(renderer.GetColorTexture());
        if (clustered == 0) referenceImage.assign(reinterpret_cast<const std::byte *>(image.data()), reinterpret_cast<const std::byte *>(image.data()) + image.size());
        else if (image.size() != referenceImage.size() || std::memcmp(image.data(), referenceImage.data(), image.size()) != 0)
            throw std::runtime_error("Clustered shadow geometry differs from whole-mesh reference");
        for (int frame = 0; frame < 32; ++frame)
        {
            casters.front().model[3].x = originalX + float(frame) * 0.01f;
            render(0);
            if (frame >= 8)
            {
                comparisonTriangles[clustered] += renderer.GetFrameStats().virtualShadows.submittedTriangles;
                comparisonGpu[clustered] += device.GetTimingStats("Scene").frameGpuMs;
                comparisonCpu[clustered] += renderer.GetTimingStats().shadowRecordingMs;
            }
        }
    }
    if (comparisonTriangles[1] >= comparisonTriangles[0])
        throw std::runtime_error("Cluster bounds did not reduce page geometry in the spatial fixture");
    std::cout << "VSM whole-mesh/clustered motion: " << comparisonTriangles[0] / 24 << " / " << comparisonTriangles[1] / 24
              << " triangles, " << comparisonGpu[0] / 24 << " / " << comparisonGpu[1] / 24 << " ms GPU frame, " << comparisonCpu[0] / 24 << " / " << comparisonCpu[1] / 24 << " ms shadow CPU\n";
    casters.front().model[3].x = originalX;
    for (int frame = 0; frame < 80; ++frame) render(0);
    (void)readPixels(renderer.GetColorTexture());
    // Immutable editor packets should skip bounds merging, while changed
    // revisions must rebuild even when the mesh allocation is unchanged.
    for (std::size_t i = 0; i < casters.size(); ++i) casters[i].preparationRevision = i + 1;
    render(0);
    render(0);
    if (!renderer.GetFrameStats().virtualShadows.clusterBoundsCacheHits ||
        renderer.GetFrameStats().virtualShadows.clusterBoundsBuilds)
        throw std::runtime_error("Stable VSM packets rebuilt cluster bounds");
    ++casters.front().preparationRevision;
    render(0);
    if (renderer.GetFrameStats().virtualShadows.clusterBoundsBuilds != 1)
        throw std::runtime_error("VSM packet revision did not rebuild its cluster bounds");
    receivers[0].preparationRevision = 10000;
    render(0); render(0);
    if (!renderer.GetFrameStats().virtualShadows.reusedPreparation)
        throw std::runtime_error("Immutable VSM scene did not reuse prepared chunks");
    ++casters.front().preparationRevision;
    render(0);
    if (renderer.GetFrameStats().virtualShadows.reusedPreparation)
        throw std::runtime_error("Changed VSM packet incorrectly reused preparation");
    if (renderer.GetFrameStats().virtualShadows.rebuiltPackets != 1 ||
        renderer.GetFrameStats().virtualShadows.reusedPackets != casters.size())
        throw std::runtime_error("A changed VSM packet rebuilt unaffected packets");
    // An inactive packet can retain a range whose chunks were overwritten by
    // another source index. Restoring it and shrinking the list must rebuild
    // that range before the next whole-preparation cache hit refreshes pointers.
    const auto originalReceiver = receivers.front();
    auto replacementReceiver = originalReceiver;
    replacementReceiver.preparationRevision = 20000;
    receivers = {BasicDraw{}, replacementReceiver};
    render(0);
    receivers = {originalReceiver};
    render(0);
    if (renderer.GetFrameStats().virtualShadows.rebuiltPackets != 1)
        throw std::runtime_error("VSM reused receiver chunks overwritten by a different packet");
    render(0);
    if (!renderer.GetFrameStats().virtualShadows.reusedPreparation)
        throw std::runtime_error("Restored receiver did not return to immutable VSM reuse");
    const auto originalCasters = casters;
    casters = {originalCasters.front()};
    render(0);
    auto replacementCaster = casters.front();
    replacementCaster.preparationRevision = 30000;
    casters = {BasicDraw{}, replacementCaster};
    render(0);
    casters = {originalCasters.front()};
    render(0);
    if (renderer.GetFrameStats().virtualShadows.rebuiltPackets != 1)
        throw std::runtime_error("VSM reused caster chunks overwritten by a different packet");
    render(0);
    if (!renderer.GetFrameStats().virtualShadows.reusedPreparation)
        throw std::runtime_error("Restored caster did not return to immutable VSM reuse");
    casters = originalCasters;
    receivers[0].preparationRevision = 0;
    for (auto &draw : casters) draw.preparationRevision = 0;
    // The shared draw table must preserve output and reduce CPU calls. Force
    // live recording by changing one caster before each warmup sequence.
    std::vector<std::byte> batchReference;
    for (int variant = 0; variant < 3; ++variant)
    {
        casters.front().alphaMode = variant ? 1 : 0;
        casters.front().baseColor.a = variant ? 0.0f : 1.0f;
        if (variant == 2)
            casters.back().instanceModels = std::make_shared<std::vector<glm::mat4>>(
                std::initializer_list<glm::mat4>{casters.back().model,
                    glm::translate(glm::mat4(1), glm::vec3(-1, 0, 0)) * casters.back().model});
        for (bool batched : {false, true})
        {
            lighting.virtualShadowBatching = batched;
            for (int frame = 0; frame < 80; ++frame) render(0);
            const auto image = readPixels(renderer.GetColorTexture());
            if (!batched) batchReference.assign(reinterpret_cast<const std::byte *>(image.data()), reinterpret_cast<const std::byte *>(image.data()) + image.size());
            else if (image.size() != batchReference.size() || std::memcmp(image.data(), batchReference.data(), image.size()) != 0)
                throw std::runtime_error("Batched VSM pages differ from individual submissions");
            casters.front().model[3].x += .001f; render(0);
            const auto stats = renderer.GetFrameStats().virtualShadows;
            if (batched && device.GetImmediateContext().MaxIndexedIndirectBatchSize() > 1 && stats.pageDrawBatches >= stats.submittedIndirectCommands)
                throw std::runtime_error("Compatible VSM chunks were not batched");
            std::cout << "VSM submission variant " << variant << (batched ? " batched: " : " individual: ")
                      << stats.pageDrawBatches << " API calls / " << stats.submittedIndirectCommands << " indirect commands\n";
            casters.front().model[3].x -= .001f;
        }
    }
    casters.front().alphaMode = 0;
    casters.front().baseColor.a = 1;
    casters.back().instanceModels.reset();
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
            std::uint64_t levelTriangles = 0;
            for (auto cost : stats.updatedTrianglesByLevel) levelTriangles += cost;
            if (levelTriangles != stats.submittedTriangles ||
                stats.triangleBudgetDeferred + stats.pageBudgetDeferred != stats.deferred ||
                stats.oldestDirtyAge > stats.historicalDirtyAge)
                throw std::runtime_error("VSM diagnostic counters are inconsistent");
            if (scenario > 0 && frame > 0 && stats.reusedFrame)
                throw std::runtime_error("Changing VSM inputs reused a stale completed frame");
            if (!renderer.GetFrameStats().virtualShadowsActive) throw std::runtime_error("GPU VSM performance path unavailable");
            const auto &frameStats = renderer.GetFrameStats();
            if (frameStats.shadowCascadeTargets || frameStats.shadowCascadeUpdates || frameStats.shadowCascadeCacheHits ||
                frameStats.shadowObjectUploads || frameStats.shadowInstances)
                throw std::runtime_error("VSM performance path retained cascade resources or work");
            if (stats.submittedTriangles > lighting.virtualShadowTriangleBudget || stats.updated > lighting.virtualShadowPageBudget ||
                stats.submittedIndirectCommands > casters.size() * mesh.GetShadowClusters().size())
                throw std::runtime_error("VSM performance budget regression");
            if (frame < 5 || !stats.gpuCountersAvailable) continue;
            const auto timing = device.GetTimingStats("Scene");
            if (timing.indexedDrawCalls > casters.size() * mesh.GetShadowClusters().size() + 2) throw std::runtime_error("VSM recorded non-VSM shadow draws");
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
