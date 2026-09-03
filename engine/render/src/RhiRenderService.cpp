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
        if (!m_sceneRenderer->Render(m_swapchain->GetWidth(), m_swapchain->GetHeight(),
                                     cameraData, lighting, commands, commands, {}, {}, texturePixelReader))
            return false;
        if (scene && scene->HasRmlRuntimeUI())
            RmlUiRuntime::Get().RenderRhi(*scene, *m_device, m_sceneRenderer->GetColorTexture(),
                                         static_cast<int>(m_swapchain->GetWidth()),
                                         static_cast<int>(m_swapchain->GetHeight()), ++m_frameSequence,
                                         cameraData.view, cameraData.projection);
        return m_swapchain->Present(m_sceneRenderer->GetColorTexture());
    }

    bool RhiRenderService::Resize(std::uint32_t width, std::uint32_t height)
    {
        return m_renderer && m_swapchain && m_swapchain->Resize(width, height) &&
               m_renderer->Resize(m_swapchain->GetWidth(), m_swapchain->GetHeight());
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
