#pragma once

#include <string>
#include <vector>

namespace PlutoGE::platform
{
    class Window;
}

namespace PlutoGE::ui
{
    class Panel;

    struct PanelUpdateTiming
    {
        std::string name;
        float updateMs = 0.0f;
        bool open = false;
        bool visible = false;
    };

    struct PanelManagerTimingStats
    {
        float beginPanelUpdateMs = 0.0f;
        float panelUpdatesTotalMs = 0.0f;
        float endPanelUpdateTotalMs = 0.0f;
        float imguiRenderMs = 0.0f;
        float platformWindowsUpdateMs = 0.0f;
        float platformWindowsRenderMs = 0.0f;
        float contextRestoreMs = 0.0f;
        int platformViewportCount = 1;
        std::vector<PanelUpdateTiming> panelUpdates;
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

        void SetEditorFontSize(float fontSize);
        [[nodiscard]] float GetEditorFontSize() const { return m_editorFontSize; }

        [[nodiscard]] const PanelManagerTimingStats &GetTimingStats() const { return m_timingStats; }

    private:
        std::vector<Panel *> m_panels;
        platform::Window *m_window = nullptr;
        PanelManagerTimingStats m_timingStats;
        float m_editorFontSize = 12.0f;
    };
}
