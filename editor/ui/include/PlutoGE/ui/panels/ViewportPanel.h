#pragma once

#include "PlutoGE/ui/panels/ViewportPanel.h"
#include "PlutoGE/ui/panels/Panel.h"

namespace PlutoGE::render
{
    class RenderTarget;
}

namespace PlutoGE::ui
{
    class EditorShell;
    class PanelManager;
    class ViewportPanel : public Panel
    {
    public:
        ViewportPanel(const PanelConfig &config) : Panel(config) {}
        ~ViewportPanel() override = default;

        void Initialize() override;
        void Render() override;
        void Shutdown() override;

        render::RenderTarget *GetRenderTarget() const { return m_renderTarget; }

    private:
        render::RenderTarget *m_renderTarget = nullptr; // The render target used for rendering the viewport content
        int m_pendingWidth = 0;
        int m_pendingHeight = 0;
        int m_resizeStableFrames = 0;
    };
}