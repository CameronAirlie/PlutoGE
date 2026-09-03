#include "PlutoGE/render/RhiRenderService.h"
#include "PlutoGE/render/RmlUiRuntime.h"
#include "PlutoGE/render/ShaderArtifacts.h"
#include "PlutoGE/scene/Scene.h"

namespace PlutoGE::render
{
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
        RmlUiRuntime::Get().Shutdown();
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

    bool RhiRenderService::RenderSceneAndPresent(const CameraData &cameraData,
                                                 const BasicLighting &lighting,
                                                 std::span<const RenderCommand> commands,
                                                 const RhiSceneRenderer::TexturePixelReader &texturePixelReader,
                                                 const scene::Scene *scene)
    {
        if (!m_swapchain || !m_renderer)
            return false;
        if (!m_sceneRenderer)
        {
            const ShaderArtifactLibrary shaderArtifacts;
            auto sceneRenderer = std::make_unique<RhiSceneRenderer>();
            if (!sceneRenderer->Initialize(*m_device, shaderArtifacts.LoadBasicRendererPackage()))
                return false;
            sceneRenderer->SetTemporalUpscalerOptions(m_upscalerOptions);
            m_sceneRenderer = std::move(sceneRenderer);
        }
        // Let the first UI frame initialize its pipelines and upload resources
        // outside an active scene command buffer. Subsequent frames can safely
        // append UI rendering to the scene submission.
        const bool combineRuntimeUiSubmission = scene && scene->HasRmlRuntimeUI() &&
                                                RmlUiRuntime::Get().IsInitialized() &&
                                                m_device->GetApi() == rhi::GraphicsApi::Vulkan;
        if (!m_sceneRenderer->Render(m_swapchain->GetWidth(), m_swapchain->GetHeight(),
                                     cameraData, lighting, commands, commands, {}, {}, texturePixelReader,
                                     PostProcessDebugView::None, !combineRuntimeUiSubmission))
            return false;
        if (scene && scene->HasRmlRuntimeUI())
            RmlUiRuntime::Get().RenderRhi(*scene, *m_device, m_sceneRenderer->GetColorTexture(),
                                         static_cast<int>(m_swapchain->GetWidth()),
                                         static_cast<int>(m_swapchain->GetHeight()), ++m_frameSequence,
                                         cameraData.view, cameraData.projection,
                                         !combineRuntimeUiSubmission);
        if (combineRuntimeUiSubmission)
            m_device->GetImmediateContext().Submit();
        return m_swapchain->Present(m_sceneRenderer->GetColorTexture());
    }

    bool RhiRenderService::Resize(std::uint32_t width, std::uint32_t height)
    {
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

    BasicMesh RhiRenderService::CreateMesh(const BasicMeshData &data)
    {
        if (!m_renderer)
            throw std::logic_error("RHI render service is not initialized");
        return m_renderer->CreateMesh(data);
    }

    void RhiRenderService::SetTemporalUpscalerOptions(rhi::TemporalUpscalerOptions options) noexcept
    {
        if (m_upscalerOptions == options)
            return;
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
