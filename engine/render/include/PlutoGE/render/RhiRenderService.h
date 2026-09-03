#pragma once

#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/RhiSceneRenderer.h"

#include <memory>

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
        [[nodiscard]] bool RenderSceneAndPresent(const CameraData &cameraData,
                                                 const BasicLighting &lighting,
                                                 std::span<const RenderCommand> commands,
                                                 const RhiSceneRenderer::TexturePixelReader &texturePixelReader = {},
                                                 const PlutoGE::scene::Scene *scene = nullptr);
        [[nodiscard]] bool IsInitialized() const noexcept { return m_renderer != nullptr; }
        [[nodiscard]] rhi::GraphicsApi GetGraphicsApi() const noexcept { return m_graphicsApi; }

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
    };
}
