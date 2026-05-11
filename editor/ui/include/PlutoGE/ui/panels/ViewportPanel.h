#pragma once

#include "PlutoGE/ui/panels/ViewportPanel.h"
#include "PlutoGE/ui/panels/Panel.h"

#include <array>
#include <glm/glm.hpp>
#include <vector>

namespace PlutoGE::render
{
    struct CameraData;
    enum class PostProcessDebugView;
    class IPostProcessEffect;
    class RenderTarget;
}

namespace PlutoGE::scene
{
    class CameraComponent;
    struct Light;
}

namespace PlutoGE::ui
{
    class EditorShell;
    class PanelManager;
    struct ViewportPanelConfig : public PanelConfig
    {
        glm::vec4 clearColor = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
    };

    class ViewportPanel : public Panel
    {
    public:
        ViewportPanel(const ViewportPanelConfig &config) : Panel(config), m_config(config) {}
        ~ViewportPanel() override = default;

        void Initialize() override;
        void Render() override;
        void ClearFrame();
        void RenderFrame(scene::CameraComponent &cameraComponent);
        void RenderFrame(const render::CameraData &cameraData, std::vector<scene::Light *> lights, const std::vector<render::IPostProcessEffect *> *postProcessEffects = nullptr);
        void Shutdown() override;
        bool ShouldRenderFrame() const;

        render::RenderTarget *GetRenderTarget() const { return GetRenderTargetForRender(); }
        bool IsViewportHovered() const { return m_isViewportHovered; }
        bool IsViewportFocused() const { return m_isViewportFocused; }
        static const char *GetDebugViewLabel(render::PostProcessDebugView debugView);

    private:
        render::RenderTarget *GetDisplayedRenderTarget() const;
        render::RenderTarget *GetRenderTargetForRender() const;
        void PresentRenderedFrame();

        ViewportPanelConfig m_config;

    private:
        std::array<render::RenderTarget *, 3> m_renderTargets{};
        int m_displayRenderTargetIndex = 0;
        int m_nextRenderTargetIndex = 0;
        int m_pendingWidth = 0;
        int m_pendingHeight = 0;
        int m_resizeStableFrames = 0;
        bool m_isViewportHovered = false;
        bool m_isViewportFocused = false;
    };
}