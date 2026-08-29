#pragma once

#include <string>
#include <memory>
#include <vector>

struct ImFont;

namespace PlutoGE::platform
{
    class Window;
}
namespace PlutoGE::render::rhi
{
    class IRenderDevice;
    class ISwapchain;
}

namespace PlutoGE::ui
{
    class Panel;
    class IEditorCompositor;

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
        PanelManager();
        ~PanelManager();

        bool InitializeImGui(platform::Window *window,
                             render::rhi::IRenderDevice *device,
                             render::rhi::ISwapchain *swapchain);
        void ShutdownImGui();

        void AddPanel(Panel *panel);

        void UpdatePanels();

        void ShutdownPanels();

        void BeginPanelUpdate();

        void EndPanelUpdate();

        void SetEditorFontSize(float fontSize);
        [[nodiscard]] float GetEditorFontSize() const { return m_editorFontSize; }
        void SetEditorFont(const std::string &fontName);
        [[nodiscard]] const std::string &GetEditorFont() const { return m_editorFont; }

        [[nodiscard]] const PanelManagerTimingStats &GetTimingStats() const { return m_timingStats; }

    private:
        std::vector<Panel *> m_panels;
        platform::Window *m_window = nullptr;
        PanelManagerTimingStats m_timingStats;
        float m_editorFontSize = 12.0f;
        std::string m_editorFont = "Martian Mono";
        ImFont *m_martianMonoFont = nullptr;
        ImFont *m_georamaFont = nullptr;
        ImFont *m_defaultFont = nullptr;
        std::string m_imguiIniPath;
        bool m_applyDefaultLayout = false;
        std::unique_ptr<IEditorCompositor> m_compositor;
    };
}
