#pragma once
#include "PlutoGE/render/rhi/Resource.h"
#include "PlutoGE/render/LoadingScreenStyle.h"
#include <vector>

namespace PlutoGE::render
{
    // Scene-independent, offscreen presentation. Hosts decide where and when to
    // composite the result; this class never pumps events or presents a window.
    // Keep it alive across scene replacement and destroy it before its device.
    class LoadingScreenRenderer
    {
    public:
        bool Initialize(rhi::IRenderDevice &device);
        void Shutdown();
        [[nodiscard]] bool IsInitialized() const noexcept { return m_device != nullptr; }
        [[nodiscard]] bool Render(std::uint32_t width, std::uint32_t height,
                                  float elapsedSeconds, unsigned stage,
                                  const LoadingScreenStyle &style = {}, bool drawDefault = true);
        [[nodiscard]] rhi::TextureHandle GetColorTexture() const noexcept { return m_color.Get(); }
        // Matches scene render targets on both backends (including Vulkan negative viewports).
        [[nodiscard]] bool IsTextureBottomUp() const noexcept
        { return true; }
    private:
        struct Vertex { float x, y; glm::vec3 color; };
        rhi::IRenderDevice *m_device = nullptr;
        rhi::GraphicsPipeline m_pipeline;
        rhi::Buffer m_vertexBuffer;
        rhi::Buffer m_parameters;
        rhi::Texture m_color;
        std::vector<Vertex> m_vertices;
        std::uint32_t m_width = 0;
        std::uint32_t m_height = 0;
    };
}
