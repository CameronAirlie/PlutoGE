#pragma once

#include "PlutoGE/render/rhi/RenderDevice.h"
#include "PlutoGE/render/rhi/Resource.h"

#include <glm/glm.hpp>

#include <memory>

namespace PlutoGE::scene
{
    class Scene;
    class UISystem;
}

namespace PlutoGE::render
{
    class UIRhiRenderer;

    // Renders a scene's native canvases into an offscreen texture for tools
    // (the UI designer). It lays the scene out with its own UI system, with
    // rect drivers off, so previews never change the scene or disturb the
    // runtime UI state.
    class NativeUIPreview
    {
    public:
        NativeUIPreview();
        ~NativeUIPreview();
        NativeUIPreview(const NativeUIPreview &) = delete;
        NativeUIPreview &operator=(const NativeUIPreview &) = delete;

        // Returns false when nothing could be rendered (no device, zero size).
        bool Render(scene::Scene &scene, rhi::IRenderDevice &device, int width, int height, const glm::vec4 &background);
        [[nodiscard]] rhi::TextureHandle GetTexture() const noexcept { return m_target.Get(); }
        [[nodiscard]] int GetWidth() const noexcept { return m_width; }
        [[nodiscard]] int GetHeight() const noexcept { return m_height; }
        // Layout of the last Render, in preview pixels (bottom-left origin).
        [[nodiscard]] scene::UISystem &GetUISystem() { return *m_ui; }
        void Shutdown();

    private:
        std::unique_ptr<scene::UISystem> m_ui;
        std::unique_ptr<UIRhiRenderer> m_renderer;
        rhi::IRenderDevice *m_device = nullptr;
        rhi::Texture m_target;
        int m_width = 0;
        int m_height = 0;
    };
}
