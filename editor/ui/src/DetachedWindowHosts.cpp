#include "PlutoGE/ui/DetachedWindowHosts.h"

#include "PlutoGE/ui/EditorWindowChrome.h"
#include "PlutoGE/ui/WindowCaption.h"

#include <imgui_internal.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr const char *kSettingsTypeName = "PlutoDetachedWindow";
        constexpr const char *kDefaultTitle = "PlutoGE";
        // An empty host waits this long, so panels being docked in are not mistaken for gone.
        constexpr int kRetireAfterEmptyFrames = 3;
        // Frames without drags or pending docks before a floating panel counts as left
        // outside: ImGui applies a drop at the start of the next frame.
        constexpr int kAdoptAfterSettledFrames = 2;
        constexpr ImGuiWindowFlags kHostFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNavFocus;

        // The ### suffix keeps the window, and the dock space inside it, stable while its title changes.
        std::string HostKey(int id)
        {
            return "###DetachedWindow" + std::to_string(id);
        }

        // Stable per host and known before the host is first submitted.
        ImGuiID DockspaceId(int id)
        {
            return ImHashStr("DockSpace", 0, ImHashStr(HostKey(id).c_str()));
        }

        void CollectWindows(const ImGuiDockNode *node, ImVector<ImGuiWindow *> &windows)
        {
            if (!node)
                return;
            for (ImGuiWindow *window : node->Windows)
                windows.push_back(window);
            CollectWindows(node->ChildNodes[0], windows);
            CollectWindows(node->ChildNodes[1], windows);
        }

        ImVector<ImGuiWindow *> HostedWindows(int id)
        {
            ImVector<ImGuiWindow *> windows;
            CollectWindows(ImGui::DockBuilderGetNode(DockspaceId(id)), windows);
            return windows;
        }

        // The selected tab of the most recently focused part of the host.
        std::string HostTitle(int id)
        {
            const ImGuiDockNode *root = ImGui::DockBuilderGetNode(DockspaceId(id));
            if (!root)
                return kDefaultTitle;
            const ImGuiDockNode *focused = ImGui::DockBuilderGetNode(root->LastFocusedNodeId);
            const ImGuiWindow *window = focused ? focused->VisibleWindow : nullptr;
            if (!window)
            {
                const auto windows = HostedWindows(id);
                window = windows.empty() ? nullptr : windows.front();
            }
            if (!window)
                return kDefaultTitle;
            const char *name = window->Name;
            return std::string(name, ImGui::FindRenderedTextEnd(name));
        }

        // A lone window, or the host of a floating dock node, that is alive and
        // owns its own OS window outside the main window.
        bool IsDetachedFloating(const ImGuiWindow *window)
        {
            if (!window || !(window->Active || window->WasActive) || window->Collapsed || !window->ViewportOwned ||
                window->Viewport == ImGui::GetMainViewport())
                return false;
            // A node host outlives its node when the node is docked elsewhere;
            // it must never be docked as if it were a panel.
            if (window->Flags & ImGuiWindowFlags_DockNodeHost)
                return window->DockNodeAsHost && window->DockNodeAsHost->IsFloatingNode();
            if (!window->DockNode)
                return true;
            // A single-panel floating node hides its host and shows the panel itself.
            return !window->DockIsActive && ImGui::DockNodeGetRootNode(window->DockNode)->IsFloatingNode();
        }
    }

    // ImGui settings callbacks; entries are host IDs, which stay valid as hosts are added.
    struct DetachedHostSettings
    {
        static DetachedWindowHosts &Hosts(ImGuiSettingsHandler *handler)
        {
            return *static_cast<DetachedWindowHosts *>(handler->UserData);
        }

        static void ClearAll(ImGuiContext *, ImGuiSettingsHandler *handler)
        {
            Hosts(handler).m_hosts.clear();
        }

        static void *ReadOpen(ImGuiContext *, ImGuiSettingsHandler *handler, const char *name)
        {
            auto &hosts = Hosts(handler);
            const int id = std::atoi(name);
            if (id <= 0 || hosts.FindHost(id))
                return nullptr;
            hosts.m_hosts.push_back({.id = id});
            return reinterpret_cast<void *>(static_cast<std::intptr_t>(id));
        }

        static void ReadLine(ImGuiContext *, ImGuiSettingsHandler *handler, void *entry, const char *line)
        {
            auto *host = Hosts(handler).FindHost(static_cast<int>(reinterpret_cast<std::intptr_t>(entry)));
            int pinned = 0;
            if (host && std::sscanf(line, "Pinned=%d", &pinned) == 1)
                host->pinned = pinned != 0;
        }

        static void WriteAll(ImGuiContext *, ImGuiSettingsHandler *handler, ImGuiTextBuffer *out)
        {
            for (const auto &host : Hosts(handler).m_hosts)
                out->appendf("[%s][%d]\nPinned=%d\n\n", kSettingsTypeName, host.id, host.pinned ? 1 : 0);
        }
    };

    DetachedWindowHosts::DetachedWindowHosts(EditorWindowChrome &chrome, PanelHooks hooks)
        : m_chrome(chrome), m_hooks(std::move(hooks))
    {
    }

    void DetachedWindowHosts::RegisterSettingsHandler()
    {
        m_hosts.clear();
        ImGuiSettingsHandler handler;
        handler.TypeName = kSettingsTypeName;
        handler.TypeHash = ImHashStr(handler.TypeName);
        handler.ClearAllFn = DetachedHostSettings::ClearAll;
        handler.ReadOpenFn = DetachedHostSettings::ReadOpen;
        handler.ReadLineFn = DetachedHostSettings::ReadLine;
        handler.WriteAllFn = DetachedHostSettings::WriteAll;
        handler.UserData = this;
        ImGui::AddSettingsHandler(&handler);
    }

    void DetachedWindowHosts::Submit(ImGuiID mainDockspaceId)
    {
        m_mainDockspaceId = mainDockspaceId;
        m_submitted = true;
        for (auto &host : m_hosts)
            SubmitHost(host);
    }

    void DetachedWindowHosts::SubmitHost(Host &host)
    {
        const std::string title = HostTitle(host.id);
        // The OS window title, shown on the taskbar and in Alt+Tab.
        const std::string name = (title == kDefaultTitle ? title : title + " - " + kDefaultTitle) + HostKey(host.id);

        ImGuiWindowClass windowClass;
        // A host is always its own OS window, even over the main window.
        windowClass.ViewportFlagsOverrideSet =
            ImGuiViewportFlags_NoAutoMerge | (host.pinned ? ImGuiViewportFlags_TopMost : ImGuiViewportFlags_None);
        ImGui::SetNextWindowClass(&windowClass);
        if (host.place)
        {
            ImGui::SetNextWindowPos(host.position);
            ImGui::SetNextWindowSize(host.size);
            host.place = false;
        }
        else
            ImGui::SetNextWindowSize(ImVec2(640.0f, 480.0f), ImGuiCond_FirstUseEver);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        // The close request of the OS window (Alt+F4, or our Close button) arrives through `open`.
        bool open = true;
        const bool visible = ImGui::Begin(name.c_str(), &open, kHostFlags);
        ImGui::PopStyleVar(3);
        if (visible)
            DrawCaption(host, title);
        const ImGuiID dockspaceId = ImGui::DockSpace(DockspaceId(host.id));
        // Never stack an adoption on another pending dock of the same frame.
        if (host.adoptWindowId != 0 && GImGui->DockContext.Requests.empty())
        {
            AdoptPayload(ImGui::FindWindowByID(host.adoptWindowId), dockspaceId);
            host.adoptWindowId = 0;
        }
        ImGui::End();
        host.closeRequested = host.closeRequested || !open;
    }

    void DetachedWindowHosts::DrawCaption(Host &host, std::string_view title)
    {
        ImGuiWindow *window = ImGui::GetCurrentWindow();
        ImGuiViewport *viewport = window->Viewport;
        const caption::Metrics metrics = caption::ComputeMetrics(true);
        const caption::Colors colors = caption::GetColors(caption::IsViewportFocused(viewport));
        const ImVec2 min = window->Pos;
        const ImVec2 max(min.x + window->Size.x, min.y + metrics.height);
        ImDrawList &drawList = *window->DrawList;
        drawList.AddRectFilled(min, max, colors.background);
        drawList.AddLine(ImVec2(min.x, max.y - 0.5f), ImVec2(max.x, max.y - 0.5f), colors.border);

        caption::LayoutBuilder layout(*viewport);
        layout.SetCaption(min, max);
        // Until the OS window exists (the first frame), the host shares another viewport.
        platform::CustomWindowFrame *frame = window->ViewportOwned ? m_chrome.FindFrame(viewport) : nullptr;
        float right = max.x;
        if (frame)
            right = caption::WindowControls(*frame, ImVec2(max.x, min.y), metrics, colors, layout);

        const float toolWidth = std::round(metrics.height * 1.15f);
        ImVec2 toolMin(right - toolWidth, min.y);
        ImVec2 toolMax(right, max.y);
        if (caption::Button("##Pin", host.pinned ? caption::Glyph::Pinned : caption::Glyph::Pin, toolMin, toolMax,
                            metrics, colors, {.checked = host.pinned},
                            host.pinned ? "Stop keeping on top" : "Keep on top"))
            SetPinned(host, !host.pinned);
        layout.AddInteractive(toolMin, toolMax);
        toolMax.x = toolMin.x;
        toolMin.x -= toolWidth;
        if (caption::Button("##DockIntoMain", caption::Glyph::DockIntoMain, toolMin, toolMax, metrics, colors, {},
                            "Dock into main window"))
            host.dockIntoMain = true;
        layout.AddInteractive(toolMin, toolMax);

        const float padding = std::round(metrics.height * 0.4f);
        const std::string shown = caption::Elide(title, toolMin.x - min.x - padding * 2.0f);
        drawList.AddText(ImVec2(min.x + padding, std::floor(min.y + (metrics.height - ImGui::GetFontSize()) * 0.5f)),
                         colors.text, shown.c_str());

        // Panels dock below the caption.
        ImGui::SetCursorScreenPos(ImVec2(min.x, max.y));
        if (frame)
            layout.Commit(*frame);
    }

    void DetachedWindowHosts::SetPinned(Host &host, bool pinned)
    {
        host.pinned = pinned;
        // ImGui only applies TopMost when it creates the OS window.
        const ImGuiWindow *window = ImGui::GetCurrentWindow();
        if (window->ViewportOwned)
            if (auto *native = static_cast<GLFWwindow *>(window->Viewport->PlatformHandle))
                glfwSetWindowAttrib(native, GLFW_FLOATING, pinned ? GLFW_TRUE : GLFW_FALSE);
        ImGui::MarkIniSettingsDirty();
    }

    void DetachedWindowHosts::AdoptPayload(ImGuiWindow *payload, ImGuiID dockspaceId)
    {
        ImGuiDockNode *node = ImGui::DockBuilderGetNode(dockspaceId);
        if (!IsDetachedFloating(payload) || !node || !node->HostWindow)
            return;
        // Docking a split node keeps its layout, but ImGui needs to know which
        // part was focused last; without that, dock its panels as tabs.
        const ImGuiDockNode *payloadNode = payload->DockNodeAsHost;
        if (payloadNode && payloadNode->IsSplitNode() && !ImGui::DockBuilderGetNode(payloadNode->LastFocusedNodeId))
        {
            ImVector<ImGuiWindow *> windows;
            CollectWindows(payloadNode, windows);
            for (const ImGuiWindow *window : windows)
                ImGui::DockBuilderDockWindow(window->Name, dockspaceId);
            return;
        }
        ImGui::DockContextQueueDock(GImGui, node->HostWindow, node, payload, ImGuiDir_None, 0.5f, false);
    }

    void DetachedWindowHosts::Update()
    {
        if (!m_submitted)
            return;
        m_submitted = false;

        for (auto &host : m_hosts)
        {
            if (std::exchange(host.dockIntoMain, false))
                for (const ImGuiWindow *window : HostedWindows(host.id))
                    ImGui::DockBuilderDockWindow(window->Name, m_mainDockspaceId);
            if (std::exchange(host.closeRequested, false))
                CloseHost(host);
            const bool empty = host.adoptWindowId == 0 && HostedWindows(host.id).empty();
            host.emptyFrames = empty ? host.emptyFrames + 1 : 0;
        }

        // Dragging the last panel out leaves its host in place until the drop.
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            const auto retired = std::erase_if(m_hosts, [](const Host &host)
            {
                if (host.emptyFrames < kRetireAfterEmptyFrames)
                    return false;
                ImGui::DockBuilderRemoveNode(DockspaceId(host.id));
                return true;
            });
            if (retired > 0)
                ImGui::MarkIniSettingsDirty();
        }

        if (m_chrome.IsEnabled())
            AdoptFloatingPanels();
    }

    void DetachedWindowHosts::CloseHost(const Host &host)
    {
        // Panels close; any other window docked here goes back to the main window.
        for (const ImGuiWindow *window : HostedWindows(host.id))
        {
            if (m_hooks.isPanel(window->Name))
                m_hooks.closePanel(window->Name);
            else
                ImGui::DockBuilderDockWindow(window->Name, m_mainDockspaceId);
        }
    }

    void DetachedWindowHosts::AdoptFloatingPanels()
    {
        // Wait until drags end and their drops are applied: a panel dropped on a
        // dock target still floats until the next frame docks it.
        const ImGuiContext &g = *GImGui;
        if (g.MovingWindow || g.DragDropActive || ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
            !g.DockContext.Requests.empty())
            m_lastBusyFrame = g.FrameCount;
        if (g.FrameCount - m_lastBusyFrame < kAdoptAfterSettledFrames)
            return;

        float captionHeight = 0.0f;
        for (ImGuiWindow *window : g.Windows)
        {
            if (!window->Active || (window->Flags & (ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_Popup)) ||
                !m_hooks.isPanel(window->Name))
                continue;
            // A floating node with a single panel hides its host and shows the panel itself.
            ImGuiWindow *payload = window;
            if (window->DockNode && window->DockIsActive)
            {
                const ImGuiDockNode *root = ImGui::DockNodeGetRootNode(window->DockNode);
                if (root->IsDockSpace())
                    continue;
                payload = root->HostWindow;
            }
            if (!IsDetachedFloating(payload) ||
                std::ranges::any_of(m_hosts, [payload](const Host &host) { return host.adoptWindowId == payload->ID; }))
                continue;

            // Grow upward by the caption so the panel stays where it was dropped.
            if (captionHeight == 0.0f)
                captionHeight = caption::ComputeMetrics(true).height;
            m_hosts.push_back({.id = AllocateId(),
                               .adoptWindowId = payload->ID,
                               .place = true,
                               .position = ImVec2(payload->Pos.x, payload->Pos.y - captionHeight),
                               .size = ImVec2(payload->Size.x, payload->Size.y + captionHeight)});
            ImGui::MarkIniSettingsDirty();
        }
    }

    void DetachedWindowHosts::DockAllInto(ImGuiID dockspaceId)
    {
        for (const auto &host : m_hosts)
            for (const ImGuiWindow *window : HostedWindows(host.id))
                ImGui::DockBuilderDockWindow(window->Name, dockspaceId);
    }

    DetachedWindowHosts::Host *DetachedWindowHosts::FindHost(int id)
    {
        const auto found = std::ranges::find(m_hosts, id, &Host::id);
        return found != m_hosts.end() ? &*found : nullptr;
    }

    // The lowest free ID, so the layout file reuses the same few window entries.
    int DetachedWindowHosts::AllocateId() const
    {
        int id = 1;
        while (std::ranges::any_of(m_hosts, [id](const Host &host) { return host.id == id; }))
            ++id;
        return id;
    }
}
