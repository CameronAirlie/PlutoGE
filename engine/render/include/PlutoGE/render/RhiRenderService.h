#pragma once

#include "PlutoGE/render/BasicRenderer.h"

#include <memory>

namespace PlutoGE::render
{
    class RhiRenderService
    {
    public:
        bool Initialize(rhi::IRenderDevice &device, rhi::ISwapchain &swapchain);
        void Shutdown();

        [[nodiscard]] bool Resize(std::uint32_t width, std::uint32_t height);
        [[nodiscard]] bool RenderAndPresent(const glm::mat4 &viewProjection,
                                            const BasicLighting &lighting,
                                            std::span<const BasicDraw> draws);
        [[nodiscard]] bool IsInitialized() const noexcept { return m_renderer != nullptr; }
        [[nodiscard]] rhi::GraphicsApi GetGraphicsApi() const noexcept { return m_graphicsApi; }

        BasicMesh CreateMesh(const BasicMeshData &data);

    private:
        rhi::ISwapchain *m_swapchain = nullptr;
        std::unique_ptr<BasicRenderer> m_renderer;
        rhi::GraphicsApi m_graphicsApi = rhi::GraphicsApi::OpenGL;
    };
}
