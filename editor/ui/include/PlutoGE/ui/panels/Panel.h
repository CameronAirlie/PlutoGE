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
        const std::string &GetName() const { return m_config.name; }
        void SetOpen(bool open) { m_isOpen = open; }
        bool WasVisibleLastFrame() const { return m_wasVisibleLastFrame; }
        // True while keyboard focus is in this panel's window or its children.
        bool HasKeyboardFocus() const;
        // Opens the panel and brings its window to the front with keyboard focus.
        void Focus();
        void SetInteractionEnabled(bool enabled) { m_isInteractionEnabled = enabled; }
        void SetVisualAlpha(float alpha) { m_visualAlpha = alpha; }
        // Fullscreen shows the panel over the whole editor work area (everything below the
        // title bar) and hides it from its dock node; restoring returns it to that node.
        bool IsFullscreen() const { return m_isFullscreen; }
        void SetFullscreen(bool fullscreen);
        void ToggleFullscreen() { SetFullscreen(!m_isFullscreen); }

        virtual void Initialize() {} // Optional initialization logic for the panel
        virtual void Render() = 0;   // Pure virtual function to render the panel
        virtual void Shutdown() {}   // Optional cleanup logic for the panel
        virtual void OnProjectChanged() {}

    private:
        void ApplyFloatingWindowState();
        void DrawFloatingWindowControls();
        void ToggleMaximized();
        bool BeginFullscreenPanel();
        void DrawFullscreenRestoreButton();
        // The ImGui window currently hosting the panel: its own, or the fullscreen one.
        std::string GetHostWindowName() const;

        PanelConfig m_config;
        bool m_isFullscreen = false;
        bool m_fullscreenChanged = false;
        int m_fullscreenBeginOrder = -1;
        bool m_isOpen = true; // Panels are open by default, can be toggled by user
        bool m_wasVisibleLastFrame = true;
        bool m_isMaximized = false;
        ImVec2 m_restorePos = ImVec2(0.0f, 0.0f);
        ImVec2 m_restoreSize = ImVec2(0.0f, 0.0f);
        bool m_isInteractionEnabled = true;
        float m_visualAlpha = 1.0f;
    };
}
