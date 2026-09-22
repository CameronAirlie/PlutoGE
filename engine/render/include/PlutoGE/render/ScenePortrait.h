#pragma once
#include "PlutoGE/render/RhiSceneRenderer.h"
#include "PlutoGE/render/RmlUiRhiRenderer.h"
#include <span>

namespace PlutoGE::render
{
    // Visual-only snapshot of a root hierarchy plus detached equipment hierarchies.
    // No entities, scripts, lights, physics or gameplay state are created or modified.
    class ScenePortrait
    {
    public:
        bool Render(rhi::IRenderDevice &device, const scene::Scene &scene, std::uint32_t root,
                    std::span<const std::uint32_t> attachments, int width, int height);
        std::shared_ptr<RmlUiRhiRenderer::ExternalTexture> Texture() const { return m_texture; }
        std::uint64_t RenderCount() const { return m_renderCount; }
    private:
        std::unique_ptr<RhiSceneRenderer> m_renderer;
        rhi::GraphicsPipeline m_copy;
        rhi::Sampler m_sampler;
        rhi::Buffer m_copyParameters;
        std::shared_ptr<RmlUiRhiRenderer::ExternalTexture> m_texture = std::make_shared<RmlUiRhiRenderer::ExternalTexture>();
        std::uint64_t m_renderCount = 0;
    };
}
