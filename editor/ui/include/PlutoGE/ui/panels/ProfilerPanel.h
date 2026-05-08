#pragma once

#include "PlutoGE/ui/EditorProfiler.h"
#include "PlutoGE/ui/panels/Panel.h"

namespace PlutoGE::render
{
    class Renderer;
}

namespace PlutoGE::ui
{
    class PanelManager;

    class ProfilerPanel : public Panel
    {
    public:
        ProfilerPanel(const PanelConfig &config, EditorProfiler *profiler, PanelManager *panelManager, render::Renderer *renderer)
            : Panel(config), m_profiler(profiler), m_panelManager(panelManager), m_renderer(renderer) {}
        ~ProfilerPanel() override = default;

        void Initialize() override;
        void Render() override;
        void Shutdown() override;

    private:
        EditorProfiler *m_profiler = nullptr;
        PanelManager *m_panelManager = nullptr;
        render::Renderer *m_renderer = nullptr;
        std::string m_lastCopiedMetrics;
    };
}