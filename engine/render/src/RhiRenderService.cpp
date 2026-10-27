#include "PlutoGE/core/CpuTrace.h"
#include "PlutoGE/render/SceneEnvironment.h"
#include "PlutoGE/render/RhiRenderService.h"
#include "PlutoGE/render/RmlUiRuntime.h"
#include "PlutoGE/render/ShaderArtifacts.h"
#include "PlutoGE/scene/Scene.h"

#include <iostream>

namespace PlutoGE::render
{
    bool RhiRenderService::SetGraphicsQuality(const GraphicsQuality &quality) noexcept
    {
        if (!quality.IsValid()) return false;
        (void)WaitForFrame();
        m_graphicsQuality = quality;
        if (m_sceneRenderer) m_sceneRenderer->SetGraphicsQuality(quality);
        m_cameraStack.SetGraphicsQuality(quality);
        m_renderTextures.SetGraphicsQuality(quality);
        return true;
    }

    void RhiRenderService::SetRenderingSettings(const RenderingSettings &settings)
    {
        (void)WaitForFrame();
        m_renderingSettings = settings;
        if (m_sceneRenderer) m_sceneRenderer->SetRenderingSettings(settings);
        m_cameraStack.SetRenderingSettings(settings);
        m_renderTextures.SetRenderingSettings(settings);
    }

    bool RhiRenderService::Initialize(rhi::IRenderDevice &device, rhi::ISwapchain &swapchain)
    {
        Shutdown();
        const ShaderArtifactLibrary shaderArtifacts;
        BasicRendererShaderPackage shaders = shaderArtifacts.LoadBasicRendererPackage();

        auto renderer = std::make_unique<BasicRenderer>();
        renderer->SetTemporalUpscalerOptions(m_upscalerOptions);
        if (!renderer->Initialize(device, shaders) || !renderer->Resize(swapchain.GetWidth(), swapchain.GetHeight()))
            return false;
        m_graphicsApi = device.GetApi();
        m_device = &device;
        m_swapchain = &swapchain;
        m_renderer = std::move(renderer);
        m_hostFrameReady = false;
        return true;
    }

    void RhiRenderService::Shutdown()
    {
        (void)WaitForFrame();
        m_renderThread.reset();
        m_runtimeUiCommands.Clear();
        m_loadingRenderer.Shutdown();
        RmlUiRuntime::Get().Shutdown();
        m_nativeUi.Shutdown();
        m_cameraStack.Shutdown();
        m_renderTextures.Shutdown();
        if (m_sceneRenderer)
            m_sceneRenderer->Shutdown();
        m_sceneRenderer.reset();
        if (m_renderer)
            m_renderer->Shutdown();
        m_renderer.reset();
        m_device = nullptr;
        m_swapchain = nullptr;
        m_frameSequence = 0;
        m_hostFrameReady = false;
    }

    bool RhiRenderService::RenderTextures(std::span<const RenderTextureView> views,
                                          const RhiSceneRenderer::TexturePixelReader &texturePixelReader,
                                          const scene::Scene *scene)
    {
        (void)WaitForFrame();
        return m_device && m_renderTextures.Render(*m_device, views, texturePixelReader, scene);
    }

    rhi::TextureHandle RhiRenderService::GetPresentedSceneTexture() const noexcept
    {
        return m_sceneRenderer ? m_sceneRenderer->GetColorTexture() : rhi::TextureHandle{};
    }

    void RhiRenderService::SetRenderThreadEnabled(bool enabled)
    {
        (void)WaitForFrame();
        if (!enabled || !m_device || !m_device->SupportsRecordingThread())
            m_renderThread.reset();
        else if (!m_renderThread)
            m_renderThread = std::make_unique<core::FrameWorker>("PlutoGE render thread");
    }

    bool RhiRenderService::WaitForFrame()
    {
        if (!m_renderThread)
            return true;
        bool succeeded = true;
        try
        {
            core::CpuScope waitScope("Render thread wait", core::CpuCategory::Wait);
            m_renderThread->Wait();
        }
        catch (const std::exception &error)
        {
            std::cerr << "The render thread failed to record or present a frame: " << error.what() << std::endl;
            succeeded = false;
        }
        // The render thread is idle: publish the completed frame's statistics.
        if (m_sceneRenderer)
        {
            m_completedTimingStats = m_sceneRenderer->GetTimingStats();
            m_completedUpscalerStatus = m_sceneRenderer->GetTemporalUpscalerStatus();
        }
        return succeeded;
    }

    void RhiRenderService::SubmitFrame(RhiSceneRenderer::PreparedFrame frame)
    {
        m_renderThread->Submit([this, frame = std::move(frame)]() mutable
        {
            // Device updates and destruction other threads request meanwhile wait for the next frame.
            m_device->SetRecordingThread(std::this_thread::get_id());
            struct ReleaseRecording
            {
                rhi::IRenderDevice &device;
                ~ReleaseRecording() { device.SetRecordingThread({}); }
            } release{*m_device};
            auto &commands = m_device->GetImmediateContext();
            try
            {
                m_sceneRenderer->Record(frame);
                m_runtimeUiCommands.Replay(commands);
                commands.Submit();
            }
            catch (...)
            {
                commands.RecoverInterruptedFrame();
                throw;
            }
            if (!m_swapchain->Present(m_sceneRenderer->GetColorTexture(), m_graphicsApi == rhi::GraphicsApi::Vulkan))
                throw std::runtime_error("The swapchain could not present the frame");
        });
    }

    bool RhiRenderService::RenderSceneAndPresent(const CameraData &cameraData,
                                                 const BasicLighting &lighting,
                                                 RenderCommandView commands,
                                                 const RhiSceneRenderer::TexturePixelReader &texturePixelReader,
                                                 const scene::Scene *scene,
                                                 std::span<IPostProcessEffect *const> postProcessEffects,
                                                 std::span<const CameraView> overlays,
                                                 std::optional<std::span<scene::Light *const>> lights)
    {
        if (!WaitForFrame())
            return false;
        if (!m_swapchain || !m_renderer)
            return false;
        if (!m_sceneRenderer)
        {
            const ShaderArtifactLibrary shaderArtifacts;
            auto sceneRenderer = std::make_unique<RhiSceneRenderer>();
            if (!sceneRenderer->Initialize(*m_device, shaderArtifacts.LoadBasicRendererPackage()))
                return false;
            sceneRenderer->SetTemporalUpscalerOptions(m_upscalerOptions);
            sceneRenderer->SetGraphicsQuality(m_graphicsQuality);
            sceneRenderer->SetRenderingSettings(m_renderingSettings);
            m_sceneRenderer = std::move(sceneRenderer);
        }
        const bool runtimeUi = cameraData.renderRuntimeUI && scene && scene->HasRmlRuntimeUI();
        const bool nativeUi = cameraData.renderRuntimeUI && scene && NativeUIRenderer::HasContent(*scene);
        const bool anyUi = runtimeUi || nativeUi;
        // Let the first UI frame initialize its pipelines and upload resources
        // outside an active scene command buffer. Subsequent frames can safely
        // append UI rendering to the scene submission.
        const bool combineRuntimeUiSubmission = anyUi && (!runtimeUi || RmlUiRuntime::Get().IsInitialized()) &&
                                                (!nativeUi || m_nativeUi.IsInitialized()) &&
                                                m_device->GetApi() == rhi::GraphicsApi::Vulkan;
        if (runtimeUi) RmlUiRuntime::Get().PrepareScenePortraits(*scene, *m_device);
        const auto atmosphere = BuildSceneAtmosphere(scene, lighting, cameraData.tagFilter);
        BasicRenderer::BeforeTemporalResolve compose;
        if (const auto *source = m_renderTextures.TakeSkinningSource())
            m_sceneRenderer->ReuseSkinningForFrame(*source);
        if (!overlays.empty())
            compose = [&](BasicRenderer &base, glm::vec2 jitter)
            {
                if (!m_cameraStack.CompositeBeforeTemporalResolve(*m_device, base, jitter, overlays, texturePixelReader, scene, m_sceneRenderer.get()))
                    throw std::runtime_error("Overlay cameras could not be composited before temporal resolve");
            };

        // Overlay cameras composite from the scene while recording, so those frames stay on this thread.
        if (m_renderThread && overlays.empty() && (!anyUi || combineRuntimeUiSubmission))
        {
            auto frame = m_sceneRenderer->Prepare(m_swapchain->GetWidth(), m_swapchain->GetHeight(), cameraData, lighting,
                                                  commands, commands, postProcessEffects, atmosphere, texturePixelReader,
                                                  PostProcessDebugView::None, false, scene, lights);
            if (!frame)
                return false;
            // The UI reads its documents now; the render thread replays what it drew after the scene.
            m_runtimeUiCommands.Clear();
            m_runtimeUiCommands.SetReplayTarget(&m_device->GetImmediateContext());
            // Native canvases draw beneath RmlUi documents.
            if (nativeUi)
                m_nativeUi.Render(*scene, *m_device, m_sceneRenderer->GetColorTexture(),
                                  static_cast<int>(m_swapchain->GetWidth()), static_cast<int>(m_swapchain->GetHeight()),
                                  cameraData.view, cameraData.projection, false, &m_runtimeUiCommands);
            if (runtimeUi)
                RmlUiRuntime::Get().RenderRhi(*scene, *m_device, m_sceneRenderer->GetColorTexture(),
                                             static_cast<int>(m_swapchain->GetWidth()),
                                             static_cast<int>(m_swapchain->GetHeight()), ++m_frameSequence,
                                             cameraData.view, cameraData.projection, false, &m_runtimeUiCommands);
            SubmitFrame(std::move(*frame));
            return true;
        }

        if (!m_sceneRenderer->Render(m_swapchain->GetWidth(), m_swapchain->GetHeight(), cameraData, lighting, commands,
                                     commands, postProcessEffects, atmosphere, texturePixelReader, PostProcessDebugView::None,
                                     !combineRuntimeUiSubmission, scene, lights, compose))
            return false;
        if (nativeUi)
            m_nativeUi.Render(*scene, *m_device, m_sceneRenderer->GetColorTexture(),
                              static_cast<int>(m_swapchain->GetWidth()), static_cast<int>(m_swapchain->GetHeight()),
                              cameraData.view, cameraData.projection, !combineRuntimeUiSubmission);
        if (runtimeUi)
            RmlUiRuntime::Get().RenderRhi(*scene, *m_device, m_sceneRenderer->GetColorTexture(),
                                         static_cast<int>(m_swapchain->GetWidth()),
                                         static_cast<int>(m_swapchain->GetHeight()), ++m_frameSequence,
                                         cameraData.view, cameraData.projection,
                                         !combineRuntimeUiSubmission);
        if (combineRuntimeUiSubmission)
            m_device->GetImmediateContext().Submit();
        if (m_renderThread)
        {
            // Keep the published statistics current for frames rendered on this thread.
            m_completedTimingStats = m_sceneRenderer->GetTimingStats();
            m_completedUpscalerStatus = m_sceneRenderer->GetTemporalUpscalerStatus();
        }
        // Scene output uses the same bottom-up texture convention as editor viewports.
        return m_swapchain->Present(m_sceneRenderer->GetColorTexture(),
                                    m_graphicsApi == rhi::GraphicsApi::Vulkan);
    }

    bool RhiRenderService::Resize(std::uint32_t width, std::uint32_t height)
    {
        (void)WaitForFrame();
        if (!m_renderer || !m_swapchain || !m_swapchain->Resize(width, height) ||
            !m_renderer->Resize(m_swapchain->GetWidth(), m_swapchain->GetHeight()))
            return false;
        m_hostFrameReady = false;
        return true;
    }

    bool RhiRenderService::RenderAndPresent(const glm::mat4 &viewProjection,
                                            const BasicLighting &lighting,
                                            std::span<const BasicDraw> draws)
    {
        (void)WaitForFrame();
        if (!m_renderer || !m_swapchain)
            return false;
        if (m_renderer->GetWidth() != m_swapchain->GetWidth() || m_renderer->GetHeight() != m_swapchain->GetHeight())
            if (!m_renderer->Resize(m_swapchain->GetWidth(), m_swapchain->GetHeight()))
                return false;
        m_renderer->Render(viewProjection, lighting, draws);
        m_hostFrameReady = true;
        return m_swapchain->Present(m_renderer->GetColorTexture());
    }

    bool RhiRenderService::Present()
    {
        (void)WaitForFrame();
        if (!m_renderer || !m_swapchain)
            return false;
        if (m_renderer->GetWidth() != m_swapchain->GetWidth() ||
            m_renderer->GetHeight() != m_swapchain->GetHeight())
        {
            if (!m_renderer->Resize(m_swapchain->GetWidth(), m_swapchain->GetHeight()))
                return false;
            m_hostFrameReady = false;
        }
        if (!m_hostFrameReady)
        {
            m_renderer->Render(glm::mat4(1.0f), BasicLighting{}, {});
            m_hostFrameReady = true;
        }
        return m_swapchain->Present(m_renderer->GetColorTexture());
    }

    bool RhiRenderService::RenderLoading(std::uint32_t width, std::uint32_t height,
                                         float elapsedSeconds, unsigned stage, const LoadingScreenStyle &style)
    {
        (void)WaitForFrame();
        if (!m_device) return false;
        if (!m_loadingRenderer.IsInitialized() && !m_loadingRenderer.Initialize(*m_device)) return false;
        return m_loadingRenderer.Render(width, height, elapsedSeconds, stage, style);
    }

    bool RhiRenderService::PresentLoading(float elapsedSeconds, unsigned stage, const LoadingScreenStyle &style)
    {
        return m_swapchain && RenderLoading(m_swapchain->GetWidth(), m_swapchain->GetHeight(), elapsedSeconds, stage, style) &&
               m_swapchain->Present(m_loadingRenderer.GetColorTexture(), m_device->GetApi() == rhi::GraphicsApi::Vulkan);
    }

    BasicMesh RhiRenderService::CreateMesh(const BasicMeshData &data)
    {
        (void)WaitForFrame();
        if (!m_renderer)
            throw std::logic_error("RHI render service is not initialized");
        return m_renderer->CreateMesh(data);
    }

    void RhiRenderService::SetTemporalUpscalerOptions(rhi::TemporalUpscalerOptions options) noexcept
    {
        if (m_upscalerOptions == options)
            return;
        (void)WaitForFrame();
        m_upscalerOptions = options;
        if (m_renderer)
            m_renderer->SetTemporalUpscalerOptions(options);
        if (m_sceneRenderer)
        {
            m_sceneRenderer->SetTemporalUpscalerOptions(options);
            m_sceneRenderer->ResetTemporalHistory();
        }
    }

    rhi::TemporalUpscalerSupport RhiRenderService::GetTemporalUpscalerSupport() const
    {
        if (!m_device)
            return {false, "The RHI render service is not initialized"};
        return m_device->GetTemporalUpscalerSupport(m_upscalerOptions.technology);
    }
}
