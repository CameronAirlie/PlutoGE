#pragma once

#include <string>

namespace PlutoGE::ui
{
    struct PanelConfig
    {
        std::string name;
    };

    class Panel
    {
    public:
        Panel(PanelConfig config) : m_config(config) {}
        virtual ~Panel() = default;

        void BeginPanel();
        void EndPanel();
        void Update();

        virtual void Initialize() {} // Optional initialization logic for the panel
        virtual void Render() = 0;   // Pure virtual function to render the panel
        virtual void Shutdown() {}   // Optional cleanup logic for the panel

    private:
        PanelConfig m_config;
        bool m_isOpen = true; // Panels are open by default, can be toggled by user
    };
}