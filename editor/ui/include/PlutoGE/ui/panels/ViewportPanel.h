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

    private:
        render::RenderTarget *m_renderTarget = nullptr;
    };
}