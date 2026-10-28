#pragma once

#include "PlutoGE/platform/CustomWindowFrame.h"

#include <imgui.h>

#include <memory>
#include <optional>
#include <unordered_map>

namespace PlutoGE::platform
{
    class Window;
}

namespace PlutoGE::ui
{
    // Owns the native frames of the editor's OS windows so that none of them
    // shows the system title bar. The main window's frame comes from
    // platform::Window (WindowConfig::customTitleBar); every detached ImGui
    // viewport that would get a system caption gets a frame here.
    //
    // Title bars themselves are drawn by EditorTitleBar (main window) and
    // DetachedWindowHosts (detached windows), which report their layout to
    // the frame returned by FindFrame.
    class EditorWindowChrome
    {
    public:
        EditorWindowChrome() = default;
        ~EditorWindowChrome();
        EditorWindowChrome(const EditorWindowChrome &) = delete;
        EditorWindowChrome &operator=(const EditorWindowChrome &) = delete;

        // Hooks ImGui's platform window callbacks. Call after the platform
        // backend is initialised, and Uninstall before it shuts down.
        void Install(platform::Window &window);
        void Uninstall();

        // True when the main window draws its own caption. Detached windows
        // only get frames in that case, so the editor never mixes styles.
        [[nodiscard]] bool IsEnabled() const;
        [[nodiscard]] platform::CustomWindowFrame *GetMainFrame() const;
        // The frame of a detached viewport, or the main frame for the main viewport.
        [[nodiscard]] platform::CustomWindowFrame *FindFrame(const ImGuiViewport *viewport) const;

        // Matches the parts of the frame the OS draws (border colour, light or
        // dark snap flyouts) to the active editor theme. Cheap when unchanged.
        void SyncAppearance();

    private:
        void OnCreateWindow(ImGuiViewport *viewport);
        void OnDestroyWindow(ImGuiViewport *viewport);
        void OnSetWindowPos(ImGuiViewport *viewport, ImVec2 position);
        void OnSetWindowSize(ImGuiViewport *viewport, ImVec2 size);
        [[nodiscard]] platform::FrameAppearance CurrentAppearance() const;

        struct PlatformHooks
        {
            void (*createWindow)(ImGuiViewport *) = nullptr;
            void (*destroyWindow)(ImGuiViewport *) = nullptr;
            void (*setWindowPos)(ImGuiViewport *, ImVec2) = nullptr;
            void (*setWindowSize)(ImGuiViewport *, ImVec2) = nullptr;
        };

        platform::Window *m_window = nullptr;
        PlatformHooks m_backend;
        std::unordered_map<ImGuiID, std::unique_ptr<platform::CustomWindowFrame>> m_frames;
        std::optional<platform::FrameAppearance> m_appliedAppearance;
    };
}
