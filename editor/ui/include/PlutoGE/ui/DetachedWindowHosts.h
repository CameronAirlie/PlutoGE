#pragma once

#include <imgui.h>

#include <functional>
#include <string_view>
#include <vector>

struct ImGuiWindow;

namespace PlutoGE::ui
{
    class EditorWindowChrome;

    // Detached editor windows. When a panel is dragged out of the main window
    // and released, a host adopts it: an OS window with the editor's own
    // title bar around a dock space, so detached panels keep tabbing,
    // splitting and docking back. The title bar adds Keep on top and Dock
    // into main window to the usual window controls.
    //
    // A host disappears once its last panel leaves or closes. Hosts are saved
    // with the layout; ImGui stores their placement and dock trees.
    //
    // Panels are only adopted while EditorWindowChrome draws custom captions.
    // Otherwise detached panels stay plain ImGui windows under a system caption.
    class DetachedWindowHosts
    {
    public:
        struct PanelHooks
        {
            // Whether an ImGui window (by name) is one of the editor's panels.
            std::function<bool(std::string_view name)> isPanel;
            std::function<void(std::string_view name)> closePanel;
        };

        DetachedWindowHosts(EditorWindowChrome &chrome, PanelHooks hooks);

        // Saves hosts with the layout. Call once per ImGui context, before a layout loads.
        void RegisterSettingsHandler();
        // Submits every host and its dock space. Call after the main dock space, before the panels.
        void Submit(ImGuiID mainDockspaceId);
        // Adopts panels released outside the main window and retires empty
        // hosts. Call after the panels, in frames that called Submit.
        void Update();
        // Moves every detached panel into `dockspaceId`, e.g. when the layout is reset.
        void DockAllInto(ImGuiID dockspaceId);

    private:
        friend struct DetachedHostSettings;

        struct Host
        {
            int id = 0;
            bool pinned = false;
            // A floating panel, or the host of a floating dock node, to dock in once the host exists.
            ImGuiID adoptWindowId = 0;
            // Placement for the first submission of a newly created host.
            bool place = false;
            ImVec2 position{};
            ImVec2 size{};
            bool dockIntoMain = false;
            bool closeRequested = false;
            int emptyFrames = 0;
        };

        void SubmitHost(Host &host);
        void DrawCaption(Host &host, std::string_view title);
        void SetPinned(Host &host, bool pinned);
        void AdoptPayload(ImGuiWindow *payload, ImGuiID dockspaceId);
        void AdoptFloatingPanels();
        void CloseHost(const Host &host);
        [[nodiscard]] Host *FindHost(int id);
        [[nodiscard]] int AllocateId() const;

        EditorWindowChrome &m_chrome;
        PanelHooks m_hooks;
        std::vector<Host> m_hosts;
        ImGuiID m_mainDockspaceId = 0;
        bool m_submitted = false;
        // Last frame with a drag or a pending dock, which defer adoption.
        int m_lastBusyFrame = 0;
    };
}
