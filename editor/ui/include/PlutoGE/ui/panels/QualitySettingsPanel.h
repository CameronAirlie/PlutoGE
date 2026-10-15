#pragma once
#include "PlutoGE/ui/panels/Panel.h"
#include "PlutoGE/render/GraphicsQuality.h"
#include <functional>
#include <utility>

namespace PlutoGE::ui
{
    class QualitySettingsPanel : public Panel
    {
    public:
        QualitySettingsPanel(const PanelConfig &config,
            std::function<void(const render::GraphicsQuality &)> preview)
            : Panel(config), m_preview(std::move(preview)) {}
        void Render() override;
        void OnProjectChanged() override { m_tier = 0; }
    private:
        int m_tier = 0;
        std::function<void(const render::GraphicsQuality &)> m_preview;
    };
}
