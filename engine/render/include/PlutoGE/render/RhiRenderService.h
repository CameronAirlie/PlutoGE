#pragma once

#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/RhiSceneRenderer.h"

#include <memory>
#include "PlutoGE/render/LoadingScreenRenderer.h"

namespace PlutoGE::scene
{
    class Scene;
}

namespace PlutoGE::render
{
    class RhiRenderService
    {
    public:
        bool Initialize(rhi::IRenderDevice &device, rhi::ISwapchain &swapchain);
        void Shutdown();
        void SetTemporalUpscalerOptions(rhi::TemporalUpscalerOptions options) noexcept;
        [[nodiscard]] rhi::TemporalUpscalerSupport GetTemporalUpscalerSupport() const;
        [[nodiscard]] const TemporalUpscalerStatus &GetTemporalUpscalerStatus() const noexcept
        {
            static const TemporalUpscalerStatus empty;
            return m_sceneRenderer ? m_sceneRenderer->GetTemporalUpscalerStatus() : empty;
        }

        [[nodiscard]] bool Resize(std::uint32_t width, std::uint32_t height);
        [[nodiscard]] bool RenderAndPresent(const glm::mat4 &viewProjection,
                                            const BasicLighting &lighting,
                                            std::span<const BasicDraw> draws);
        // Presents the persistent host target without recording another scene.
        // The target is initialized lazily and refreshed only after a resize.
        [[nodiscard]] bool Present();
        // Render-only path for embedded hosts. Does not touch the swapchain or host target.
        [[nodiscard]] bool RenderLoading(std::uint32_t width, std::uint32_t height,
                                         float elapsedSeconds, unsigned stage, const LoadingScreenStyle &style = {});
        [[nodiscard]] const LoadingScreenRenderer &GetLoadingRenderer() const noexcept { return m_loadingRenderer; }
        // Standalone convenience: render at window size and present the result.
        [[nodiscard]] bool PresentLoading(float elapsedSeconds, unsigned stage, const LoadingScreenStyle &style = {});
        [[nodiscard]] rhi::TextureHandle GetHostColorTexture() const noexcept
        { return m_renderer ? m_renderer->GetColorTexture() : rhi::TextureHandle{}; }
        [[nodiscard]] bool RenderSceneAndPresent(const CameraData &cameraData,
                                                 const BasicLighting &lighting,
                                                 std::span<const RenderCommand> commands,
                                                 const RhiSceneRenderer::TexturePixelReader &texturePixelReader = {},
                                                 const PlutoGE::scene::Scene *scene = nullptr,
                                                 std::span<IPostProcessEffect *const> postProcessEffects = {});
        [[nodiscard]] bool IsInitialized() const noexcept { return m_renderer != nullptr; }
        [[nodiscard]] rhi::GraphicsApi GetGraphicsApi() const noexcept { return m_graphicsApi; }

        [[nodiscard]] const RhiSceneTimingStats &GetTimingStats() const noexcept
        {
            static const RhiSceneTimingStats empty;
            return m_sceneRenderer ? m_sceneRenderer->GetTimingStats() : empty;
        }

        BasicMesh CreateMesh(const BasicMeshData &data);

    private:
        rhi::IRenderDevice *m_device = nullptr;
        rhi::ISwapchain *m_swapchain = nullptr;
        std::unique_ptr<BasicRenderer> m_renderer;
        std::unique_ptr<RhiSceneRenderer> m_sceneRenderer;
        rhi::GraphicsApi m_graphicsApi = rhi::GraphicsApi::OpenGL;
        std::uint64_t m_frameSequence = 0;
        rhi::TemporalUpscalerOptions m_upscalerOptions;
        bool m_hostFrameReady = false;
        LoadingScreenRenderer m_loadingRenderer;
    };
}
