#pragma once

#include <imgui.h>

#include <string>

namespace PlutoGE::ui
{
    struct PanelConfig
    {
        std::string name;
        bool openByDefault = true;
    };

    class Panel
    {
    public:
        Panel(PanelConfig config) : m_config(config), m_isOpen(config.openByDefault) {}
        virtual ~Panel() = default;

        bool BeginPanel();
        void EndPanel();
        void Update();
        bool IsOpen() const { return m_isOpen; }
        void SetOpen(bool open) { m_isOpen = open; }
        bool WasVisibleLastFrame() const { return m_wasVisibleLastFrame; }
        void SetInteractionEnabled(bool enabled) { m_isInteractionEnabled = enabled; }
        void SetVisualAlpha(float alpha) { m_visualAlpha = alpha; }

        virtual void Initialize() {} // Optional initialization logic for the panel
        virtual void Render() = 0;   // Pure virtual function to render the panel
        virtual void Shutdown() {}   // Optional cleanup logic for the panel

    private:
        void ApplyFloatingWindowState();
        void DrawFloatingWindowControls();
        void ToggleMaximized();

        PanelConfig m_config;
        bool m_isOpen = true; // Panels are open by default, can be toggled by user
        bool m_wasVisibleLastFrame = true;
        bool m_isMaximized = false;
        ImVec2 m_restorePos = ImVec2(0.0f, 0.0f);
        ImVec2 m_restoreSize = ImVec2(0.0f, 0.0f);
        bool m_isInteractionEnabled = true;
        float m_visualAlpha = 1.0f;
    };
}