#pragma once
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
    // composites them, in order, over a base camera's final colour.
    //
    // Each overlay is rendered by its own scene renderer so its depth buffer,
    // temporal history and caches stay independent of the base view. Coverage
    // is taken from overlay depth, so opaque and alpha-tested geometry
    // composites; transparent-only overlay surfaces and particles do not.
    class RhiCameraStackCompositor
    {
    public:
        RhiCameraStackCompositor();
        ~RhiCameraStackCompositor();
        RhiCameraStackCompositor(const RhiCameraStackCompositor &) = delete;
        RhiCameraStackCompositor &operator=(const RhiCameraStackCompositor &) = delete;

        void Shutdown();

        // `target` must be the base scene renderer's display output, already
        // submitted. With submit=false the composite is left recording so the
        // caller can append work (such as runtime UI) before one final submit.
        bool Composite(rhi::IRenderDevice &device, rhi::TextureHandle target,
                       std::uint32_t width, std::uint32_t height,
                       std::span<const CameraView> overlays,
                       const RhiSceneRenderer::TexturePixelReader &texturePixelReader,
                       const scene::Scene *scene, bool submit = true);

    private:
        RhiSceneRenderer *AcquireOverlayRenderer(std::size_t index);
        bool EnsureCompositePipeline();

        rhi::IRenderDevice *m_device = nullptr;
        std::vector<std::unique_ptr<RhiSceneRenderer>> m_overlayRenderers;
        rhi::GraphicsPipeline m_compositePipeline;
        rhi::Sampler m_sampler;
        rhi::Buffer m_parameters;
    };
}
