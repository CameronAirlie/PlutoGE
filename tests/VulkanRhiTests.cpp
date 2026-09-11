#include "RenderOptimizationChecks.h"
#include "OpaqueBatchingChecks.h"
#include "TemporalMotionRenderingChecks.h"
#include "GlassRenderingChecks.h"
#include "Fsr2RenderingChecks.h"
#include "ParticlePointRenderingChecks.h"
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/rhi/vulkan/VulkanDevice.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "ShadowFilteringChecks.h"
#include "SsrRenderingChecks.h"
#include "TextureMipRenderingChecks.h"
#include "VctWorldCacheRenderingChecks.h"
#include "VirtualShadowPerformanceChecks.h"
#include "VsmOnlyRenderingChecks.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

#include <glm/gtc/matrix_transform.hpp>

namespace
{
    std::vector<std::uint32_t> ReadSpirv(const char *name)
    {
        std::ifstream input(std::filesystem::path(PLUTO_RHI_TEST_SHADER_DIR) / name, std::ios::binary | std::ios::ate);
        if (!input)
            return {};
        const auto size = input.tellg();
        std::vector<std::uint32_t> words(static_cast<std::size_t>(size) / sizeof(std::uint32_t));
        input.seekg(0);
        input.read(reinterpret_cast<char *>(words.data()), size);
        return words;
    }
}

int main(int argc, char **argv)
{
    using namespace PlutoGE::render;
    try
    {
        rhi::vulkan::VulkanDevice device;
        BasicRendererShaderPackage shaders;
        shaders.vertex.spirv = ReadSpirv("BasicLit.vertex.spv");
        shaders.instancedVertex.spirv = ReadSpirv("BasicLitInstanced.vertex.spv");
        shaders.fragment.spirv = ReadSpirv("BasicLit.fragment.spv");
        shaders.transparentFragment.spirv = ReadSpirv("Glass.fragment.spv");
        shaders.glassSceneCopy.vertex.spirv = ReadSpirv("GlassSceneCopy.vertex.spv");
        shaders.glassSceneCopy.fragment.spirv = ReadSpirv("GlassSceneCopy.fragment.spv");
        shaders.shadowVertex.spirv = ReadSpirv("DirectionalShadow.vertex.spv");
        shaders.shadowInstancedVertex.spirv = ReadSpirv("DirectionalShadowInstanced.vertex.spv");
        shaders.shadowFragment.spirv = ReadSpirv("DirectionalShadow.fragment.spv");
        shaders.maskedShadowFragment.spirv = ReadSpirv("DirectionalShadowMasked.fragment.spv");
        const std::array<const char *, 7> vsmCompute{"VSMReset", "VSMRequest", "VSMAllocate", "VSMSignature", "VSMBudget", "VSMBin", "VSMPublish"};
        for (std::size_t index = 0; index < vsmCompute.size(); ++index)
            shaders.virtualShadows.compute[index].spirv = ReadSpirv((std::string(vsmCompute[index]) + ".compute.spv").c_str());
        const std::array<const char *, 5> vsmRaster{"VSMReceiver", "VSMPage", "VSMClear", "VSMReceiverRigid", "VSMPageRigid"};
        for (std::size_t index = 0; index < vsmRaster.size(); ++index)
        {
            shaders.virtualShadows.raster[index * 2].spirv = ReadSpirv((std::string(vsmRaster[index]) + ".vertex.spv").c_str());
            shaders.virtualShadows.raster[index * 2 + 1].spirv = ReadSpirv((std::string(vsmRaster[index]) + ".fragment.spv").c_str());
        }
        shaders.displayOutput.vertex.spirv = ReadSpirv("DisplayOutput.vertex.spv");
        shaders.displayOutput.fragment.spirv = ReadSpirv("DisplayOutput.fragment.spv");
        const auto loadPostProcess = [&](BasicPostProcessEffectType type, const char *module)
        {
            auto &shader = shaders.postProcess[static_cast<std::size_t>(type)];
            shader.vertex.spirv = ReadSpirv((std::string(module) + ".vertex.spv").c_str());
            shader.fragment.spirv = ReadSpirv((std::string(module) + ".fragment.spv").c_str());
        };
        loadPostProcess(BasicPostProcessEffectType::TAA, "TAA");
        loadPostProcess(BasicPostProcessEffectType::SSR, "SSR");
        loadPostProcess(BasicPostProcessEffectType::VolumetricFog, "VolumetricFog");
        loadPostProcess(BasicPostProcessEffectType::ToneMapping, "ToneMapping");
        loadPostProcess(BasicPostProcessEffectType::GammaCorrection, "GammaCorrection");
        loadPostProcess(BasicPostProcessEffectType::FXAA, "FXAA");
        loadPostProcess(BasicPostProcessEffectType::ColorGrading, "ColorGrading");
        loadPostProcess(BasicPostProcessEffectType::ChromaticAberration, "ChromaticAberration");
        loadPostProcess(BasicPostProcessEffectType::LensFlare, "LensFlare");
        loadPostProcess(BasicPostProcessEffectType::MotionBlur, "MotionBlur");
        constexpr std::array<const char *, 4> bloomModules{
            "BloomPrefilter", "BloomDownsample", "BloomUpsample", "BloomComposite"};
        for (std::size_t index = 0; index < bloomModules.size(); ++index)
        {
            shaders.bloom[index].vertex.spirv = ReadSpirv((std::string(bloomModules[index]) + ".vertex.spv").c_str());
            shaders.bloom[index].fragment.spirv = ReadSpirv((std::string(bloomModules[index]) + ".fragment.spv").c_str());
        }
        constexpr std::array<const char *, 3> ssaoModules{
            "SSAO", "SSAOResolve", "SSAOComposite"};
        for (std::size_t index = 0; index < ssaoModules.size(); ++index)
        {
            shaders.ssao[index].vertex.spirv = ReadSpirv((std::string(ssaoModules[index]) + ".vertex.spv").c_str());
            shaders.ssao[index].fragment.spirv = ReadSpirv((std::string(ssaoModules[index]) + ".fragment.spv").c_str());
        }
        shaders.vctCompute[0].spirv = ReadSpirv("VCTResolve.compute.spv");
        shaders.vctCompute[1].spirv = ReadSpirv("VCTDirectionalMip.compute.spv");
        shaders.vctCompute[2].spirv = ReadSpirv("VCTProbeUpdate.compute.spv");
        shaders.vctCompute[3].spirv = ReadSpirv("VCTBounceUpdate.compute.spv");
        shaders.vctVoxelization.vertexShader.spirv = ReadSpirv("VCTVoxelize.vertex.spv");
        shaders.vctVoxelization.geometryShader.spirv = ReadSpirv("VCTVoxelize.geometry.spv");
        shaders.vctVoxelization.fragmentShader.spirv = ReadSpirv("VCTVoxelize.fragment.spv");
        constexpr std::array<const char *, 3> vctModules{"VCTConeTrace", "VCTTemporal", "VCTMetadata"};
        for (std::size_t index = 0; index < vctModules.size(); ++index)
        {
            shaders.vctPostProcess[index].vertex.spirv = ReadSpirv((std::string(vctModules[index]) + ".vertex.spv").c_str());
            shaders.vctPostProcess[index].fragment.spirv = ReadSpirv((std::string(vctModules[index]) + ".fragment.spv").c_str());
        }
        shaders.particles.vertexShader.spirv = ReadSpirv("Particles.vertex.spv");
        shaders.particles.fragmentShader.spirv = ReadSpirv("Particles.fragment.spv");
        LoadRenderOptimizationShaders(shaders);
        BasicRenderer renderer;
        if (!renderer.Initialize(device, shaders) || !renderer.Resize(96, 64))
            return 1;

        if (argc > 1 && std::string_view(argv[1]) == "--render-optimizations")
        {
            CheckRenderOptimizations(renderer, device, shaders,
                [&](auto texture) { return device.ReadTextureRgba8(texture); });
            return 0;
        }
        if (argc > 1 && std::string_view(argv[1]) == "--opaque-batching")
        {
            CheckOpaqueBatching(renderer, [&](auto texture) { return device.ReadTextureRgba8(texture); });
            return 0;
        }
        if (argc > 1 && std::string_view(argv[1]) == "--fsr2-only")
        {
            CheckFsr2Rendering(renderer, device);
            return 0;
        }
        if (argc > 1 && std::string_view(argv[1]) == "--temporal-motion")
        {
            CheckTemporalMotionRendering(renderer, device, false, [&](auto texture) { return device.ReadTextureRgba8(texture); });
            if (device.GetTemporalUpscalerSupport(rhi::TemporalUpscaler::Fsr2).supported)
                CheckTemporalMotionRendering(renderer, device, true, [&](auto texture) { return device.ReadTextureRgba8(texture); });
            return 0;
        }
        if (argc > 1 && std::string_view(argv[1]) == "--particles-points-only")
        {
            CheckParticlePointRendering(renderer, [&](rhi::TextureHandle texture) {
                return device.ReadTextureRgba8(texture);
            });
            return 0;
        }
        if (argc > 1 && std::string_view(argv[1]) == "--vct-world-cache")
        {
            CheckVctWorldCacheRendering(renderer, [&](rhi::TextureHandle texture)
            {
                return device.ReadTextureRgba8(texture);
            });
            CheckVctSecondaryBounce(renderer, [&](rhi::TextureHandle texture) { return device.ReadTextureRgba8(texture); });
            CheckVctSmallEmitters(renderer, [&](rhi::TextureHandle texture)
            {
                return device.ReadTextureRgba8(texture);
            });
            return 0;
        }

        if (argc > 1 && std::string_view(argv[1]) == "--vsm-only")
        {
            PlutoGE::scene::LightComponent light;
            light.GetLight().type = PlutoGE::scene::LightType::Directional;
            light.GetLight().castsShadows = true;
            light.GetLight().directionalShadowSettings.method = ShadowMethod::Virtual;
            light.GetLight().activeShadowCascadeCount = 4;
            light.Initialize();
            if (light.GetLight().activeShadowCascadeCount != 0 ||
                std::any_of(light.GetLight().shadowCascadeMaps.begin(), light.GetLight().shadowCascadeMaps.end(),
                            [](const auto &map) { return bool(map); }))
                throw std::runtime_error("VSM light component retained legacy cascade state");
            CheckVsmOnlyRendering(renderer, [&](rhi::TextureHandle texture) { return device.ReadTextureRgba8(texture); });
            return 0;
        }
        if (argc > 1 && std::string_view(argv[1]) == "--vsm-performance")
        {
            CheckVirtualShadowPerformance(renderer, device, [&](rhi::TextureHandle texture) { return device.ReadTextureRgba8(texture); });
            return 0;
        }
        if (argc > 1 && std::string_view(argv[1]) == "--ssr-only")
        {
            // Exercise odd extents and replacement of pooled trace/resolve
            // targets when the editor viewport changes size.
            for (const auto size : {rhi::Extent2D{127, 95}, rhi::Extent2D{256, 192}})
            {
                renderer.Resize(size.width, size.height);
                CheckSsrRendering(renderer, [&](rhi::TextureHandle texture)
                {
                    return device.ReadTextureRgba8(texture);
                });
            }
            return 0;
        }
        if (argc > 1 && std::string_view(argv[1]) == "--ssr-performance")
        {
            renderer.Resize(1222, 796);
            // Optional raw RGBA8 snapshots support before/after shader comparisons.
            std::ofstream snapshots;
            if (argc > 2)
            {
                snapshots.open(argv[2], std::ios::binary);
                if (!snapshots) throw std::runtime_error("Cannot open SSR snapshot output");
            }
            CheckSsrRendering(renderer, [&](rhi::TextureHandle texture)
            {
                auto pixels = device.ReadTextureRgba8(texture);
                if (snapshots.is_open())
                    snapshots.write(reinterpret_cast<const char *>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
                return pixels;
            }, &device);
            return 0;
        }
        CheckTextureMipRendering(renderer, device, [&](rhi::TextureHandle texture) {
            return device.ReadTextureRgba8(texture);
        }, argc > 1 && std::string_view(argv[1]) == "--texture-mips");
        if (argc > 1 && std::string_view(argv[1]) == "--texture-mips") return 0;
        CheckShadowFiltering(renderer, [&](rhi::TextureHandle texture)
        {
            return device.ReadTextureRgba8(texture);
        });
        if (argc > 1 && std::string_view(argv[1]) == "--shadows-only")
            return 0;

        // Nested effect timings must preserve parent end queries, including
        // when deeply nested instrumentation exhausts the query budget.
        {
            auto &commands = device.GetImmediateContext();
            for (int frame = 0; frame < 6; ++frame)
            {
                commands.BeginFrame("NestedTimingTest");
                commands.BeginGpuScope("parent");
                for (int depth = 0; depth < 70; ++depth) commands.BeginGpuScope("child");
                for (int depth = 0; depth < 70; ++depth) commands.EndGpuScope();
                commands.EndGpuScope();
                commands.Submit();
            }
            const auto timing = device.GetTimingStats("NestedTimingTest");
            if (!timing.hasGpuResult || timing.gpuScopes.size() < 2 || timing.gpuScopes.back().name != "parent")
                throw std::runtime_error("Nested GPU timing lost its parent or failed to resolve");
            commands.SetGpuProfilingEnabled(false);
            for (int frame = 0; frame < 6; ++frame)
            {
                commands.BeginFrame("NestedTimingTest");
                commands.BeginGpuScope("disabled");
                commands.EndGpuScope();
                commands.Submit();
                const auto disabledTiming = device.GetTimingStats("NestedTimingTest");
                if (disabledTiming.hasGpuResult || !disabledTiming.gpuScopes.empty() || disabledTiming.frameGpuMs != 0.0f)
                    throw std::runtime_error("Disabled GPU profiling published stale timings");
            }
            commands.SetGpuProfilingEnabled(true);
            for (int frame = 0; frame < 6; ++frame)
            {
                commands.BeginFrame("NestedTimingTest");
                commands.BeginGpuScope("restored");
                commands.EndGpuScope();
                commands.Submit();
            }
            const auto restoredTiming = device.GetTimingStats("NestedTimingTest");
            if (!restoredTiming.hasGpuResult || restoredTiming.gpuScopes.size() != 1 || restoredTiming.gpuScopes.front().name != "restored")
                throw std::runtime_error("GPU profiling failed to resume");
        }

        CheckParticlePointRendering(renderer, [&](rhi::TextureHandle texture) {
            return device.ReadTextureRgba8(texture);
        });
        CheckGlassRendering(renderer, [&](rhi::TextureHandle texture)
        {
            return device.ReadTextureRgba8(texture);
        });

        // The Vulkan editor host owns its presentation renderer and creates
        // independent off-screen renderers for its viewports on the same device.
        // Keep this lifecycle covered so pipeline creation and cache invalidation
        // cannot make switching the editor default backend unsafe.
        {
            BasicRenderer viewportRenderer;
            if (!viewportRenderer.Initialize(device, shaders))
                return 11;
        }

        // Post-process graph changes can retire last frame's transient images
        // while the next frame is being recorded. Cache invalidation must defer
        // recycling that active frame's descriptor pool rather than throwing
        // from a noexcept resource destructor.
        const auto retiredTexture = device.CreateTexture(
            {4, 4, rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::ColorAttachment,
             "Descriptor invalidation regression", true});
        device.GetImmediateContext().BeginFrame();
        device.DestroyTexture(retiredTexture);
        device.GetImmediateContext().Submit();

        constexpr std::array<BasicVertex, 8> vertices = {{
            {{{-0.5f, -0.5f, -0.5f}}, {{-0.577f, -0.577f, -0.577f}}, {{0, 0}}},
            {{{0.5f, -0.5f, -0.5f}}, {{0.577f, -0.577f, -0.577f}}, {{1, 0}}},
            {{{0.5f, 0.5f, -0.5f}}, {{0.577f, 0.577f, -0.577f}}, {{1, 1}}},
            {{{-0.5f, 0.5f, -0.5f}}, {{-0.577f, 0.577f, -0.577f}}, {{0, 1}}},
            {{{-0.5f, -0.5f, 0.5f}}, {{-0.577f, -0.577f, 0.577f}}, {{0, 0}}},
            {{{0.5f, -0.5f, 0.5f}}, {{0.577f, -0.577f, 0.577f}}, {{1, 0}}},
            {{{0.5f, 0.5f, 0.5f}}, {{0.577f, 0.577f, 0.577f}}, {{1, 1}}},
            {{{-0.5f, 0.5f, 0.5f}}, {{-0.577f, 0.577f, 0.577f}}, {{0, 1}}},
        }};
        constexpr std::array<std::uint32_t, 36> indices = {
            0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 4, 7, 0, 7, 3,
            1, 2, 6, 1, 6, 5, 3, 7, 6, 3, 6, 2, 0, 1, 5, 0, 5, 4};
        auto cube = renderer.CreateMesh({vertices, indices});
        constexpr std::array<BasicVertex, 8> cornerVertices = {{
            {{{-2.0f, 0.0f, 0.0f}}, {{0, 1, 0}}, {{0, 0}}},
            {{{2.0f, 0.0f, 0.0f}}, {{0, 1, 0}}, {{1, 0}}},
            {{{2.0f, 0.0f, 2.0f}}, {{0, 1, 0}}, {{1, 1}}},
            {{{-2.0f, 0.0f, 2.0f}}, {{0, 1, 0}}, {{0, 1}}},
            {{{-2.0f, 0.0f, 0.0f}}, {{0, 0, 1}}, {{0, 0}}},
            {{{2.0f, 0.0f, 0.0f}}, {{0, 0, 1}}, {{1, 0}}},
            {{{2.0f, 3.0f, 0.0f}}, {{0, 0, 1}}, {{1, 1}}},
            {{{-2.0f, 3.0f, 0.0f}}, {{0, 0, 1}}, {{0, 1}}},
        }};
        constexpr std::array<std::uint32_t, 12> cornerIndices = {
            0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7};
        auto corner = renderer.CreateMesh({cornerVertices, cornerIndices});

        glm::mat4 projection(0.0f);
        constexpr float nearPlane = 0.1f, farPlane = 100.0f;
        const float focal = 1.0f / glm::tan(glm::radians(50.0f) * 0.5f);
        projection[0][0] = focal / (96.0f / 64.0f);
        projection[1][1] = focal;
        projection[2][2] = nearPlane / (farPlane - nearPlane);
        projection[2][3] = -1.0f;
        projection[3][2] = farPlane * nearPlane / (farPlane - nearPlane);
        const glm::mat4 view = glm::lookAtRH(glm::vec3(2.5f, 1.8f, 3.0f), glm::vec3(0), glm::vec3(0, 1, 0));
        const auto cubeInstances = std::make_shared<const std::vector<glm::mat4>>(
            std::vector<glm::mat4>{
                glm::translate(glm::mat4(1), glm::vec3(-0.65f, 0, 0)),
                glm::translate(glm::mat4(1), glm::vec3(0.65f, 0, 0))});
        const std::array draws{
            BasicDraw{.mesh = &cube, .instanceModels = cubeInstances},
            BasicDraw{.mesh = &corner},
        };
        BasicLighting neutralLighting;
        neutralLighting.view = view;
        neutralLighting.cameraPosition = glm::vec3(glm::inverse(view)[3]);
        neutralLighting.ambientIntensity = 1.0f;
        neutralLighting.directionalIntensity = 0.0f;
        neutralLighting.shadowsEnabled = true;
        if (argc > 1 && std::string_view(argv[1]) == "--benchmark")
        {
            std::vector<BasicDraw> manyDraws(1532, BasicDraw{.mesh = &cube});
            double geometryMs = 0.0, shadowMs = 0.0;
            for (int frame = 0; frame < 120; ++frame)
            {
                renderer.Render(projection * view, neutralLighting, manyDraws);
                if (frame >= 20)
                {
                    geometryMs += renderer.GetTimingStats().geometryRecordingMs;
                    shadowMs += renderer.GetTimingStats().shadowRecordingMs;
                }
            }
            const auto stats = device.GetTimingStats();
            std::cout << "1532 shared-material draws: geometry CPU " << geometryMs / 100.0
                      << " ms, shadow CPU " << shadowMs / 100.0 << " ms, uniform bytes "
                      << stats.uniformBytesUploaded << ", descriptor binds " << stats.descriptorBindCalls << "\n";
            return 0;
        }
        renderer.Render(projection * view, neutralLighting, draws);
        const auto &frameStats = renderer.GetFrameStats();
        if (frameStats.geometryDraws != draws.size() || frameStats.geometryInstances != 3 ||
            frameStats.shadowCandidates != draws.size() || frameStats.shadowObjectUploads > 3)
        {
            std::cerr << "BasicRenderer reported inconsistent recorded-work statistics\n";
            return 12;
        }
        const auto deviceStats = device.GetTimingStats();
        if (deviceStats.indexedDrawCalls < frameStats.geometryDraws ||
            deviceStats.descriptorBindCalls == 0)
        {
            std::cerr << "Vulkan RHI did not report recorded draw and descriptor bind counts\n";
            return 13;
        }
        renderer.Render(projection * view, neutralLighting, draws);
        const auto &cachedFrameStats = renderer.GetFrameStats();
        if (cachedFrameStats.shadowCascadeCacheHits != neutralLighting.shadowCascadeCount ||
            cachedFrameStats.shadowCascadeUpdates != 0 || cachedFrameStats.ShadowDraws() != 0 ||
            cachedFrameStats.shadowObjectUploads != 0)
        {
            std::cerr << "BasicRenderer did not reuse unchanged directional shadow cascades\n";
            return 14;
        }
        auto movedDraws = draws;
        movedDraws[1].model[3][0] += 0.125f;
        renderer.Render(projection * view, neutralLighting, movedDraws);
        if (renderer.GetFrameStats().shadowCascadeUpdates != neutralLighting.shadowCascadeCount)
        {
            std::cerr << "Moving a shadow caster did not invalidate cached cascades\n";
            return 16;
        }
        auto movedInstances = std::make_shared<std::vector<glm::mat4>>(*cubeInstances);
        movedDraws[0].instanceModels = movedInstances;
        renderer.Render(projection * view, neutralLighting, movedDraws);
        if (renderer.GetFrameStats().shadowCascadeUpdates != 0)
        {
            std::cerr << "Equivalent instance transforms invalidated cached shadows\n";
            return 18;
        }
        // Mutate the same allocation: pointer identity is not a content version.
        (*movedInstances)[0][3][0] += 0.125f;
        renderer.Render(projection * view, neutralLighting, movedDraws);
        if (renderer.GetFrameStats().shadowCascadeUpdates != neutralLighting.shadowCascadeCount)
        {
            std::cerr << "Moving a shadow instance did not invalidate cached cascades\n";
            return 17;
        }
        renderer.Render(projection * view, neutralLighting, draws);
        auto movedShadowLighting = neutralLighting;
        movedShadowLighting.shadowMatrices[0][3][0] += 0.25f;
        renderer.Render(projection * view, movedShadowLighting, draws);
        const auto &invalidatedFrameStats = renderer.GetFrameStats();
        if (invalidatedFrameStats.shadowCascadeUpdates != 1 ||
            invalidatedFrameStats.shadowCascadeCacheHits + invalidatedFrameStats.shadowCascadeUpdates !=
                neutralLighting.shadowCascadeCount)
        {
            std::cerr << "BasicRenderer shadow cache invalidation was not cascade-local\n";
            return 15;
        }
        renderer.Render(projection * view, neutralLighting, draws);
        const auto pixels = device.ReadTextureRgba8(renderer.GetColorTexture());
        const auto normalPixels = device.ReadTextureRgba8(renderer.GetNormalTexture());
        const auto materialPixels = device.ReadTextureRgba8(renderer.GetMaterialTexture());
        const auto motionPixels = device.ReadTextureRgba8(renderer.GetMotionTexture());
        if (normalPixels.size() != pixels.size() || materialPixels.size() != pixels.size() || motionPixels.size() != pixels.size())
        {
            std::cerr << "Vulkan G-buffer attachments returned inconsistent extents\n";
            return 7;
        }

        std::size_t changed = 0;
        std::uint64_t red = 0, green = 0, blue = 0;
        for (std::size_t i = 0; i + 3 < pixels.size(); i += 4)
        {
            const auto r = std::to_integer<unsigned char>(pixels[i]);
            const auto g = std::to_integer<unsigned char>(pixels[i + 1]);
            const auto b = std::to_integer<unsigned char>(pixels[i + 2]);
            if (r > 30 || g > 30 || b > 35)
            {
                ++changed;
                red += r;
                green += g;
                blue += b;
            }
        }
        if (changed < 100)
        {
            std::cerr << "Vulkan BasicRenderer produced a blank image (" << changed << " changed pixels)\n";
            return 2;
        }
        // A white material under white lighting must remain neutral. This
        // catches channel/order and constant-buffer layout regressions that can
        // otherwise make every Vulkan viewport mesh appear red.
        if (red > green + changed * 3 || red > blue + changed * 3)
        {
            std::cerr << "Vulkan BasicRenderer introduced a red channel bias ("
                      << red << ", " << green << ", " << blue << ")\n";
            return 4;
        }
        renderer.Render(projection * view, neutralLighting, draws, {}, {}, PostProcessDebugView::Lod);
        const auto lodDebugPixels = device.ReadTextureRgba8(renderer.GetColorTexture());
        std::size_t blueLodPixels = 0;
        for (std::size_t i = 0; i + 3 < lodDebugPixels.size(); i += 4)
        {
            const auto r = std::to_integer<unsigned char>(lodDebugPixels[i]);
            const auto g = std::to_integer<unsigned char>(lodDebugPixels[i + 1]);
            const auto b = std::to_integer<unsigned char>(lodDebugPixels[i + 2]);
            if (b > r + 40 && b > g + 20)
                ++blueLodPixels;
        }
        if (blueLodPixels < 100)
        {
            std::cerr << "Vulkan RHI LOD debug view did not produce diagnostic output ("
                      << blueLodPixels << " blue pixels)\n";
            return 10;
        }
        auto ssao = BasicPostProcessEffect{BasicPostProcessEffectType::SSAO};
        ssao.quality = 32;
        ssao.parameters[0] = {1.5f, 0.02f, 3.0f, 1.0f};
        ssao.parameters[1] = {1.0f, 1.0f, 0.0f, 0.0f};
        ssao.parameters[2] = {0.02f, 0.85f, 0.0f, 0.0f};
        renderer.Render(projection * view, neutralLighting, draws, std::span(&ssao, 1));
        const auto aoPixels = device.ReadTextureRgba8(renderer.GetColorTexture());
        std::size_t occludedPixels = 0;
        for (std::size_t i = 0; i + 3 < aoPixels.size(); i += 4)
        {
            const auto value = std::to_integer<unsigned char>(aoPixels[i]);
            if (value < 254)
                ++occludedPixels;
        }
        if (occludedPixels < 10)
        {
            std::cerr << "Vulkan SSAO produced an all-white AO diagnostic ("
                      << occludedPixels << " occluded pixels)\n";
            return 8;
        }
        auto fog = BasicPostProcessEffect{BasicPostProcessEffectType::VolumetricFog};
        fog.quality = 16;
        fog.parameters[0] = {0.8f, 0.85f, 0.9f, 0.02f};
        fog.parameters[1] = {0.05f, 0.0f, 40.0f, 0.65f};
        fog.parameters[2] = {0.2f, 1.0f, 6.0f, 0.92f};
        fog.parameters[3].x = 2.0f;
        auto fogLighting = neutralLighting;
        fogLighting.directionalIntensity = 2.0f;
        renderer.Render(projection * view, fogLighting, draws, std::span(&fog, 1));
        if (device.ReadTextureRgba8(renderer.GetColorTexture()).size() != 96u * 64u * 4u)
        {
            std::cerr << "Vulkan volumetric fog returned an invalid image\n";
            return 17;
        }
        auto vctgi = BasicPostProcessEffect{BasicPostProcessEffectType::VCTGI};
        vctgi.quality = 3;
        vctgi.parameters[0] = {16.0f, 1.0f, 0.5f, 32.0f};
        vctgi.parameters[1] = {0.1f, 0.9f, 0.05f, 0.8f};
        vctgi.parameters[2] = {32.0f, 1.0f, 1.0f, 1.0f};
        vctgi.parameters[3].w = 256.0f;
        vctgi.parameters[4] = {1.0f, 48.0f, 256.0f, 0.0f};
        for (int frame = 0; frame < 72; ++frame)
            renderer.Render(projection * view, neutralLighting, draws, std::span(&vctgi, 1));
        if (device.ReadTextureRgba8(renderer.GetColorTexture()).size() != 96u * 64u * 4u)
        {
            std::cerr << "Vulkan VCTGI returned an invalid image\n";
            return 9;
        }
        auto colorGrading = BasicPostProcessEffect{BasicPostProcessEffectType::ColorGrading};
        colorGrading.parameters[0] = {0.0f, 1.05f, 1.05f, 0.04f};
        colorGrading.parameters[1] = {0.0f, 0.05f, 0.0f, 1.0f};
        colorGrading.parameters[2] = {1.0f, 0.0f, 0.1f, 0.01f};
        auto chromaticAberration = BasicPostProcessEffect{BasicPostProcessEffectType::ChromaticAberration};
        chromaticAberration.parameters[0].x = 0.003f;
        auto bloom = BasicPostProcessEffect{BasicPostProcessEffectType::Bloom};
        bloom.quality = 4;
        bloom.parameters[0] = {0.35f, 0.8f, 0.5f, 1.0f};
        auto lensFlare = BasicPostProcessEffect{BasicPostProcessEffectType::LensFlare};
        lensFlare.parameters[0] = {0.2f, 0.8f, 1.0f, 0.55f};
        auto motionBlur = BasicPostProcessEffect{BasicPostProcessEffectType::MotionBlur};
        motionBlur.parameters[0] = {1.0f, 0.5f, 20.0f, 0.35f};
        motionBlur.parameters[1].x = 1.0f;
        const std::array postEffects{
            bloom,
            lensFlare,
            motionBlur,
            BasicPostProcessEffect{BasicPostProcessEffectType::ToneMapping, 1.0f, 2.2f},
            BasicPostProcessEffect{BasicPostProcessEffectType::GammaCorrection, 1.0f, 2.2f},
            BasicPostProcessEffect{BasicPostProcessEffectType::FXAA, 1.0f, 2.2f, 1},
            colorGrading,
            chromaticAberration,
        };
        renderer.Render(projection * view, neutralLighting, draws, postEffects);
        const auto postProcessedPixels = device.ReadTextureRgba8(renderer.GetColorTexture());
        if (postProcessedPixels.size() != 96u * 64u * 4u)
        {
            std::cerr << "Vulkan post-process chain returned an invalid image\n";
            return 6;
        }

        // Input-cache hits must notice changes to bounds and caster eligibility,
        // even when neither transforms nor cascade matrices have changed.
        std::array boundedDraws{BasicDraw{.mesh = &cube, .shadowBoundsRadius = 0.1f}};
        renderer.Render(projection * view, neutralLighting, boundedDraws);
        renderer.Render(projection * view, neutralLighting, boundedDraws);
        if (renderer.GetFrameStats().shadowCascadeUpdates != 0) return 21;
        boundedDraws[0].shadowBoundsCenter = glm::vec3(100.0f);
        renderer.Render(projection * view, neutralLighting, boundedDraws);
        if (renderer.GetFrameStats().shadowCascadeUpdates != neutralLighting.shadowCascadeCount) return 22;
        boundedDraws[0].shadowBoundsCenter = glm::vec3(0.0f);
        renderer.Render(projection * view, neutralLighting, boundedDraws);
        if (renderer.GetFrameStats().shadowCascadeUpdates != neutralLighting.shadowCascadeCount) return 23;
        boundedDraws[0].castsShadow = false;
        renderer.Render(projection * view, neutralLighting, boundedDraws);
        if (renderer.GetFrameStats().shadowCascadeUpdates != neutralLighting.shadowCascadeCount) return 24;
        boundedDraws[0].castsShadow = true;
        renderer.Render(projection * view, neutralLighting, boundedDraws);
        if (renderer.GetFrameStats().shadowCascadeUpdates != neutralLighting.shadowCascadeCount) return 25;

        // Shared submesh uniforms must reduce uploads without changing pixels.
        // Different UV scales are an equivalent reference for untextured draws
        // but force separate material allocations.
        auto sharedLighting = neutralLighting;
        sharedLighting.shadowsEnabled = false;
        std::vector<BasicDraw> sharedDraws(32, BasicDraw{.mesh = &cube});
        renderer.Render(projection * view, sharedLighting, sharedDraws);
        const auto sharedUploadBytes = device.GetTimingStats().uniformBytesUploaded;
        const auto sharedPixels = device.ReadTextureRgba8(renderer.GetColorTexture());
        auto separateMaterials = sharedDraws;
        for (std::size_t index = 0; index < separateMaterials.size(); ++index)
            separateMaterials[index].uvScale = glm::vec2(float(index + 1));
        renderer.Render(projection * view, sharedLighting, separateMaterials);
        const auto separateUploadBytes = device.GetTimingStats().uniformBytesUploaded;
        if (sharedUploadBytes >= separateUploadBytes ||
            renderer.GetFrameStats().geometryDraws != sharedDraws.size() ||
            device.ReadTextureRgba8(renderer.GetColorTexture()) != sharedPixels)
        {
            std::cerr << "Shared material uniforms did not preserve rendering and reduce uploads\n";
            return 19;
        }
        // Reuse must not overwrite earlier draws or retain last frame's values.
        std::array coloredDraws{
            BasicDraw{.mesh = &cube, .model = (*cubeInstances)[0], .baseColor = {1, 0, 0, 1}},
            BasicDraw{.mesh = &cube, .model = (*cubeInstances)[1], .baseColor = {0, 0, 1, 1}}};
        renderer.Render(projection * view, sharedLighting, coloredDraws);
        const auto coloredPixels = device.ReadTextureRgba8(renderer.GetColorTexture());
        std::swap(coloredDraws[0], coloredDraws[1]);
        renderer.Render(projection * view, sharedLighting, coloredDraws);
        if (device.ReadTextureRgba8(renderer.GetColorTexture()) != coloredPixels || coloredPixels == sharedPixels)
        {
            std::cerr << "Per-draw material or transform data changed with draw order\n";
            return 20;
        }

        CheckSsrRendering(renderer, [&](rhi::TextureHandle texture)
        {
            return device.ReadTextureRgba8(texture);
        });

        // Exercise the editor's persistent readback allocation across a
        // render-target resize. This also verifies that retiring the old
        // texture waits for its targeted copy without violating VMA's
        // persistent-map ownership.
        (void)device.ReadTextureRgba8Buffered(renderer.GetColorTexture());
        if (!renderer.Resize(64, 48))
            return 5;
        renderer.Render(projection * view, neutralLighting, draws);
        (void)device.ReadTextureRgba8Buffered(renderer.GetColorTexture());
        std::cout << "Vulkan mesh rendered on " << device.GetDeviceName() << " (" << changed << " changed pixels)\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 3;
    }
    return 0;
}
