#pragma once

#include "PlutoGE/ui/EditorViewportOverlay.h"
#include <functional>
#include <filesystem>
#include <string>
#include <string_view>
#include <memory>
#include <vector>
#include "PlutoGE/ui/DetachedWindowHosts.h"
#include "PlutoGE/ui/EditorCompositor.h"
#include "PlutoGE/ui/EditorFonts.h"
#include "PlutoGE/ui/EditorTitleBar.h"
#include "PlutoGE/ui/EditorWindowChrome.h"

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
        void OnProjectChanged();
        // Saves the current layout to the previous file, then loads `path` (or
        // the default layout when it does not exist). Empty disables persistence.
        void SetLayoutPath(const std::filesystem::path &path);
        void ResetLayout() { m_applyDefaultLayout = true; }

        void BeginPanelUpdate(bool workspace = true);

        void EndPanelUpdate();
        // Replays the completed editor frame with one viewport overlay. No panel
        // updates, ImGui NewFrame, scene access or input handling occur here.
        void PresentViewportOverlay(const EditorViewportRegion &region, EditorTextureHandle texture,
                                    bool bottomUp, const std::function<void()> &presentHost);

        void SetEditorFontSize(float fontSize);
        [[nodiscard]] float GetEditorFontSize() const { return m_editorFontSize; }
        void SetEditorFont(fonts::EditorFont font);
        [[nodiscard]] fonts::EditorFont GetEditorFont() const { return m_editorFont; }

        // The main window's title bar, which hosts the main menu.
        [[nodiscard]] EditorTitleBar &GetTitleBar() { return m_titleBar; }

        [[nodiscard]] const PanelManagerTimingStats &GetTimingStats() const { return m_timingStats; }
        [[nodiscard]] EditorTextureHandle RegisterTexture(const EditorTextureDescriptor &descriptor);
        void UpdateTexture(EditorTextureHandle texture, const EditorTextureDescriptor &descriptor);
        void UnregisterTexture(EditorTextureHandle texture);
        [[nodiscard]] std::uint64_t GetImGuiTextureId(EditorTextureHandle texture) const noexcept;

    private:
        [[nodiscard]] Panel *FindPanel(std::string_view name) const;

        std::vector<Panel *> m_panels;
        platform::Window *m_window = nullptr;
        PanelManagerTimingStats m_timingStats;
        float m_editorFontSize = fonts::kBaseSize;
        fonts::EditorFont m_editorFont = fonts::kDefaultFont;
        fonts::EditorFontLibrary m_fonts;
        void LoadLayout();
        void SaveLayout();

        std::filesystem::path m_layoutPath;
        bool m_applyDefaultLayout = false;
        bool m_layoutLoadPending = false;
        bool m_panelsUpdated = false;
        std::unique_ptr<IEditorCompositor> m_compositor;
        EditorWindowChrome m_windowChrome;
        EditorTitleBar m_titleBar{m_windowChrome};
        DetachedWindowHosts m_detachedHosts;
    };
}
