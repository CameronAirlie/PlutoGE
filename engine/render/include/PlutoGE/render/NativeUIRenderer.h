#pragma once

#include "PlutoGE/render/rhi/RenderDevice.h"

#include <glm/glm.hpp>

#include <memory>

namespace PlutoGE::scene
{
    class Scene;
}

namespace PlutoGE::render
{
    class UIRhiRenderer;

    // Draws a scene's native (RectTransform) canvases onto a render target.
    // Owned per render service: each keeps its own retained geometry.
    class NativeUIRenderer
    {
    public:
        NativeUIRenderer();
        ~NativeUIRenderer();
        NativeUIRenderer(const NativeUIRenderer &) = delete;
        NativeUIRenderer &operator=(const NativeUIRenderer &) = delete;

        // True when the scene has an active native canvas to draw.
        [[nodiscard]] static bool HasContent(const scene::Scene &scene);
        // False until the first Render creates the pipeline.
        [[nodiscard]] bool IsInitialized() const noexcept { return m_renderer != nullptr; }
        // Releases GPU resources; call before the device is destroyed.
        void Shutdown();

        // Lays out and paints the scene's canvases for a width x height target
        // and records them into `commands`, or into the device's immediate
        // context when null (beginning and submitting a frame when
        // `manageSubmission` is set). The camera places world-space canvases.
        void Render(const scene::Scene &scene, rhi::IRenderDevice &device, rhi::TextureHandle target,
                    int width, int height, const glm::mat4 &view, const glm::mat4 &projection,
                    bool manageSubmission, rhi::ICommandContext *commands = nullptr);

    private:
        rhi::IRenderDevice *m_device = nullptr;
        std::unique_ptr<UIRhiRenderer> m_renderer;
    };
}
