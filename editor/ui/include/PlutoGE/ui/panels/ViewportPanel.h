#pragma once

#include "PlutoGE/ui/panels/ViewportPanel.h"
#include "PlutoGE/ui/panels/Panel.h"

#include <glm/glm.hpp>

namespace PlutoGE::render
{
    class RenderTarget;
    struct CameraData;
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
        void RenderFrame(render::CameraData &cameraData);
        void Shutdown() override;

        render::RenderTarget *GetRenderTarget() const { return m_renderTarget; }

    private:
        ViewportPanelConfig m_config;

    private:
        render::RenderTarget *m_renderTarget = nullptr; // The render target used for rendering the viewport content
        int m_pendingWidth = 0;
        int m_pendingHeight = 0;
        int m_resizeStableFrames = 0;
    };
}