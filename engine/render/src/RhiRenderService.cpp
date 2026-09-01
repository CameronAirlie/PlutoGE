#include "PlutoGE/render/RhiRenderService.h"
#include "PlutoGE/render/ShaderArtifacts.h"

namespace PlutoGE::render
{
    bool RhiRenderService::Initialize(rhi::IRenderDevice &device, rhi::ISwapchain &swapchain)
    {
        Shutdown();
        const ShaderArtifactLibrary shaderArtifacts;
        BasicRendererShaderPackage shaders = shaderArtifacts.LoadBasicRendererPackage();

        auto renderer = std::make_unique<BasicRenderer>();
        if (!renderer->Initialize(device, shaders) || !renderer->Resize(swapchain.GetWidth(), swapchain.GetHeight()))
            return false;
        auto sceneRenderer = std::make_unique<RhiSceneRenderer>();
        if (!sceneRenderer->Initialize(device, shaders))
        {
            renderer->Shutdown();
            return false;
        }
        m_graphicsApi = device.GetApi();
        m_swapchain = &swapchain;
        m_renderer = std::move(renderer);
        m_sceneRenderer = std::move(sceneRenderer);
        return true;
    }

    void RhiRenderService::Shutdown()
    {
        if (m_sceneRenderer)
            m_sceneRenderer->Shutdown();
        m_sceneRenderer.reset();
        if (m_renderer)
            m_renderer->Shutdown();
        m_renderer.reset();
        m_swapchain = nullptr;
    }

    bool RhiRenderService::RenderSceneAndPresent(const CameraData &cameraData,
                                                 const BasicLighting &lighting,
                                                 std::span<const RenderCommand> commands,
                                                 const RhiSceneRenderer::TexturePixelReader &texturePixelReader)
    {
        if (!m_sceneRenderer || !m_swapchain)
            return false;
        if (!m_sceneRenderer->Render(m_swapchain->GetWidth(), m_swapchain->GetHeight(),
                                     cameraData, lighting, commands, commands, {}, {}, texturePixelReader))
            return false;
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
}
