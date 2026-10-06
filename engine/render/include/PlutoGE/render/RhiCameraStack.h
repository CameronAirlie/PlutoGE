#pragma once
#include "PlutoGE/render/GraphicsQuality.h"
#include "PlutoGE/render/CameraView.h"
#include "PlutoGE/render/RhiSceneRenderer.h"

#include <memory>
#include <span>
#include <vector>

namespace PlutoGE::scene
{
    class Scene;
}

namespace PlutoGE::render
{
    // Renders overlay cameras (for example a first-person weapon camera) and
    // composites them, in order, into a base camera's HDR temporal inputs.
    //
    // Each overlay is rendered by its own scene renderer so its depth buffer,
    // motion history and caches stay independent of the base view. The shared
    // temporal resolve runs after composition. Depth supplies opaque coverage;
    // premultiplied alpha supplies particle coverage without replacing world
    // depth or motion metadata.
    class RhiCameraStackCompositor
    {
    public:
        bool SetGraphicsQuality(const GraphicsQuality &quality) noexcept
        {
            if (!quality.IsValid()) return false;
            m_graphicsQuality = quality;
            return true;
        }
        RhiCameraStackCompositor();
        ~RhiCameraStackCompositor();
        RhiCameraStackCompositor(const RhiCameraStackCompositor &) = delete;
        RhiCameraStackCompositor &operator=(const RhiCameraStackCompositor &) = delete;

        void Shutdown();

        // Called at the base renderer's temporal boundary, after its HDR segment
        // was submitted. Leaves composition recording for the shared resolve.
        bool CompositeBeforeTemporalResolve(rhi::IRenderDevice &device, BasicRenderer &base, glm::vec2 clipJitter,
                       std::span<const CameraView> overlays,
                       const RhiSceneRenderer::TexturePixelReader &texturePixelReader, const scene::Scene *scene);

        // `target` must be the base scene renderer's display output, already
        // submitted. With submit=false the composite is left recording so the
        // caller can append work (such as runtime UI) before one final submit.
        bool Composite(rhi::IRenderDevice &device, rhi::TextureHandle target,
                       std::uint32_t width, std::uint32_t height,
                       std::span<const CameraView> overlays,
                       const RhiSceneRenderer::TexturePixelReader &texturePixelReader,
                       const scene::Scene *scene, bool submit = true);

    private:
        GraphicsQuality m_graphicsQuality;
        RhiSceneRenderer *AcquireOverlayRenderer(std::size_t index);
        bool EnsureCompositePipeline();
        bool EnsureTemporalCompositePipelines();

        rhi::IRenderDevice *m_device = nullptr;
        std::vector<std::unique_ptr<RhiSceneRenderer>> m_overlayRenderers;
        rhi::GraphicsPipeline m_compositePipeline;
        rhi::Sampler m_sampler;
        rhi::Buffer m_parameters;
        rhi::GraphicsPipeline m_temporalCompositePipeline;
        rhi::GraphicsPipeline m_temporalTransparencyPipeline;
        rhi::GraphicsPipeline m_temporalMetadataPipeline;
        rhi::Texture m_temporalMetadata;
        rhi::Extent2D m_metadataSize;
        std::vector<rhi::Buffer> m_temporalParameters;
        std::vector<const void *> m_historyKeys;
    };
}
