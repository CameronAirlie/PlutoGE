#pragma once
#include "PlutoGE/render/GraphicsQuality.h"
#include "PlutoGE/render/CameraView.h"
#include "PlutoGE/render/RhiSceneRenderer.h"

#include <memory>
#include <span>
#include <unordered_map>
#include <utility>

namespace PlutoGE::scene
{
    class Scene;
}

namespace PlutoGE::render
{
    class RenderTexture;

    // A camera rendering into a render texture this frame.
    struct RenderTextureView
    {
        RenderTexture *target = nullptr;
        CameraView view;
    };

    // Renders cameras into render textures before the frame's on-screen views,
    // then publishes each image on its RenderTexture for materials to sample.
    //
    // Every target keeps its own scene renderer and temporal history; skinning
    // is shared across this scene frame. A material sampling a texture rendered
    // in the same pass sees the previous frame's image.
    class RhiRenderTextureRenderer
    {
    public:
        bool SetGraphicsQuality(const GraphicsQuality &quality) noexcept
        {
            if (!quality.IsValid()) return false;
            m_graphicsQuality = quality;
            return true;
        }
        RhiRenderTextureRenderer();
        ~RhiRenderTextureRenderer();
        RhiRenderTextureRenderer(const RhiRenderTextureRenderer &) = delete;
        RhiRenderTextureRenderer &operator=(const RhiRenderTextureRenderer &) = delete;

        // Unpublishes every live render texture this renderer drew.
        void Shutdown();

        // Submits its own frames; call while no frame is recording, once per
        // scene frame before the on-screen view (also when views is empty).
        bool Render(rhi::IRenderDevice &device, std::span<const RenderTextureView> views,
                    const RhiSceneRenderer::TexturePixelReader &texturePixelReader, const scene::Scene *scene);

        // Consume only for the on-screen view immediately following Render.
        // Poses must remain immutable until all views in the scene frame finish.
        const RhiSceneRenderer *TakeSkinningSource() noexcept
        {
            return std::exchange(m_skinningSource, nullptr);
        }

    private:
        GraphicsQuality m_graphicsQuality;
        struct Target
        {
            std::weak_ptr<const void> lifetime;
            std::unique_ptr<RhiSceneRenderer> renderer;
            rhi::Texture image;
            std::uint32_t width = 0, height = 0;
        };

        Target *AcquireTarget(RenderTexture &texture);
        bool EnsureResolvePipeline();
        void ReleaseExpiredTargets();

        rhi::IRenderDevice *m_device = nullptr;
        const RhiSceneRenderer *m_skinningSource = nullptr;
        std::unordered_map<RenderTexture *, Target> m_targets;
        rhi::GraphicsPipeline m_resolvePipeline;
        rhi::Sampler m_sampler;
        rhi::Buffer m_parameters;
    };
}
