#pragma once

#include <vector>

namespace PlutoGE::platform
{
    class Window;
}

namespace PlutoGE::ui
{
    class Panel;
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

    private:
        std::vector<Panel *> m_panels;
    };
}