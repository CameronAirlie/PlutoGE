#pragma once

#include <vector>

namespace PlutoGE::platform
{
    class Window;
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

        bool InitializeImGui(platform::Window *window);

        void AddPanel(Panel *panel);

        void UpdatePanels();

        void ShutdownPanels();

        void BeginPanelUpdate();

        void EndPanelUpdate();

        [[nodiscard]] const PanelManagerTimingStats &GetTimingStats() const { return m_timingStats; }

    private:
        std::vector<Panel *> m_panels;
        PanelManagerTimingStats m_timingStats;
    };
}