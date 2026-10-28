#pragma once

#include "PlutoGE/ui/WindowCaption.h"

#include <imgui.h>

#include <optional>
#include <string>
#include <string_view>

namespace PlutoGE::ui
{
    class EditorWindowChrome;

    // The main window's title bar, which also hosts the editor's main menu.
    // Laid out like a Windows 11 app with an integrated menu: the PlutoGE mark
    // (system menu), the menus, the project name, whatever the editor adds,
    // a status line and the window controls. Everything that is not a
    // control drags the window, snaps it and maximises it on double-click.
    //
    // Without a custom frame (other platforms, or a borderless window) the
    // bar degrades to an ordinary main menu bar below the system caption.
    //
    // Per frame:
    //     if (titleBar.BeginMenuBar())
    //     {
    //         ...ImGui::BeginMenu() calls...
    //         titleBar.EndMenus();
    //         ...custom widgets, each followed by titleBar.AddInteractiveItem()...
    //         titleBar.Status(message);
    //         titleBar.EndMenuBar();
    //     }
    //     ...
    //     titleBar.Finish(); // Every frame, also when no menus were submitted.
    class EditorTitleBar
    {
    public:
        explicit EditorTitleBar(EditorWindowChrome &chrome) : m_chrome(chrome) {}

        // Shown beside the menus, since the system caption no longer shows the window title.
        void SetDocumentTitle(std::string title, bool modified);
        // Blends the bar toward an accent, e.g. while play mode is running.
        void SetTint(std::optional<ImVec4> tint) { m_tint = tint; }

        // Replaces ImGui::BeginMainMenuBar. Returns false when nothing should be submitted.
        bool BeginMenuBar();
        // Ends the leading menus, which stay clickable; the document title follows them.
        void EndMenus();
        // Keeps the last submitted item clickable instead of dragging the window.
        void AddInteractiveItem();
        // Right-aligned before the window controls and elided to fit; the full text shows on hover.
        void Status(std::string_view text);
        void EndMenuBar();

        // Draws the window controls and reports the caption to the native frame.
        void Finish();

    private:
        bool BeginBarWindow();
        void DrawSystemMenuButton();
        void DrawDocumentTitle();

        EditorWindowChrome &m_chrome;
        std::string m_title;
        bool m_modified = false;
        std::optional<ImVec4> m_tint;

        // Reset after every Finish.
        struct FrameState
        {
            bool begun = false;
            bool custom = false;
            bool inMenuBar = false;
            bool inMenus = false;
            float controlsLeft = 0.0f;
            caption::Metrics metrics;
            caption::Colors colors;
            std::optional<caption::LayoutBuilder> layout;
        };
        FrameState m_frame;
    };
}
