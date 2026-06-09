#pragma once

#include "PlutoGE/render/RenderBackend.h"

#include <vector>

namespace PlutoGE::platform
{
    class Window;
}

namespace PlutoGE::render
{
    class Renderer;
}

namespace PlutoGE::ui
{
    class Panel;

    struct PanelManagerTimingStats
    {
        float endPanelUpdateTotalMs = 0.0f;
        float imguiRenderMs = 0.0f;
        float platformWindowsUpdateMs = 0.0f;
        float platformWindowsRenderMs = 0.0f;
        float contextRestoreMs = 0.0f;
        int platformViewportCount = 1;
    };

    class PanelManager
    {
    public:
        PanelManager() = default;
        ~PanelManager() = default;

        bool InitializeImGui(platform::Window *window, render::Renderer *renderer);

        void AddPanel(Panel *panel);

        void UpdatePanels();

        void ShutdownPanels();

        void BeginPanelUpdate();

        void EndPanelUpdate();

        [[nodiscard]] const PanelManagerTimingStats &GetTimingStats() const { return m_timingStats; }

    private:
        render::Renderer *m_renderer = nullptr;
        render::RenderBackend m_backend = render::RenderBackend::OpenGL;
        bool m_imguiInitialized = false;
        std::vector<Panel *> m_panels;
        PanelManagerTimingStats m_timingStats;
    };
}