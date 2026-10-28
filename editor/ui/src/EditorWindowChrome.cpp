#include "PlutoGE/ui/EditorWindowChrome.h"

#include "PlutoGE/platform/Window.h"
#include "PlutoGE/ui/EditorTheme.h"

#include <GLFW/glfw3.h>

#include <algorithm>

namespace PlutoGE::ui
{
    namespace
    {
        // ImGui platform hooks are plain function pointers; they reach the
        // installed chrome through this.
        EditorWindowChrome *g_installedChrome = nullptr;

        std::uint32_t ToRgb(const ImVec4 &color)
        {
            const auto channel = [](float value)
            { return static_cast<std::uint32_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f); };
            return (channel(color.x) << 16) | (channel(color.y) << 8) | channel(color.z);
        }

        bool SameAppearance(const platform::FrameAppearance &a, const platform::FrameAppearance &b)
        {
            return a.dark == b.dark && a.borderColor == b.borderColor;
        }
    }

    EditorWindowChrome::~EditorWindowChrome()
    {
        Uninstall();
    }

    void EditorWindowChrome::Install(platform::Window &window)
    {
        Uninstall();
        m_window = &window;
        if (!IsEnabled() || g_installedChrome)
            return;

        auto &platformIO = ImGui::GetPlatformIO();
        m_backend = {platformIO.Platform_CreateWindow, platformIO.Platform_DestroyWindow,
                     platformIO.Platform_SetWindowPos, platformIO.Platform_SetWindowSize};
        if (!m_backend.createWindow || !m_backend.destroyWindow || !m_backend.setWindowPos || !m_backend.setWindowSize)
        {
            m_backend = {};
            return;
        }
        g_installedChrome = this;
        platformIO.Platform_CreateWindow = [](ImGuiViewport *viewport) { g_installedChrome->OnCreateWindow(viewport); };
        platformIO.Platform_DestroyWindow = [](ImGuiViewport *viewport) { g_installedChrome->OnDestroyWindow(viewport); };
        platformIO.Platform_SetWindowPos = [](ImGuiViewport *viewport, ImVec2 position)
        { g_installedChrome->OnSetWindowPos(viewport, position); };
        platformIO.Platform_SetWindowSize = [](ImGuiViewport *viewport, ImVec2 size)
        { g_installedChrome->OnSetWindowSize(viewport, size); };
    }

    void EditorWindowChrome::Uninstall()
    {
        // Frames detach before their windows are destroyed by the backend.
        m_frames.clear();
        if (g_installedChrome == this)
        {
            if (ImGui::GetCurrentContext())
            {
                auto &platformIO = ImGui::GetPlatformIO();
                platformIO.Platform_CreateWindow = m_backend.createWindow;
                platformIO.Platform_DestroyWindow = m_backend.destroyWindow;
                platformIO.Platform_SetWindowPos = m_backend.setWindowPos;
                platformIO.Platform_SetWindowSize = m_backend.setWindowSize;
            }
            g_installedChrome = nullptr;
        }
        m_backend = {};
        m_appliedAppearance.reset();
        m_window = nullptr;
    }

    bool EditorWindowChrome::IsEnabled() const
    {
        return GetMainFrame() != nullptr;
    }

    platform::CustomWindowFrame *EditorWindowChrome::GetMainFrame() const
    {
        auto *frame = m_window ? m_window->GetCustomFrame() : nullptr;
        return frame && frame->IsActive() ? frame : nullptr;
    }

    platform::CustomWindowFrame *EditorWindowChrome::FindFrame(const ImGuiViewport *viewport) const
    {
        if (!viewport)
            return nullptr;
        if (viewport == ImGui::GetMainViewport())
            return GetMainFrame();
        const auto found = m_frames.find(viewport->ID);
        return found != m_frames.end() && found->second->IsActive() ? found->second.get() : nullptr;
    }

    platform::FrameAppearance EditorWindowChrome::CurrentAppearance() const
    {
        const EditorTheme &theme = GetActiveEditorTheme();
        return {.dark = !theme.light, .borderColor = ToRgb(theme.palette.border)};
    }

    void EditorWindowChrome::SyncAppearance()
    {
        auto *main = GetMainFrame();
        if (!main)
            return;
        const auto appearance = CurrentAppearance();
        if (m_appliedAppearance && SameAppearance(*m_appliedAppearance, appearance))
            return;
        main->SetAppearance(appearance);
        for (const auto &[id, frame] : m_frames)
            frame->SetAppearance(appearance);
        m_appliedAppearance = appearance;
    }

    void EditorWindowChrome::OnCreateWindow(ImGuiViewport *viewport)
    {
        m_backend.createWindow(viewport);
        // Popups, menus and tooltips are created undecorated and stay that way.
        auto *native = static_cast<GLFWwindow *>(viewport->PlatformHandle);
        if (!native || (viewport->Flags & ImGuiViewportFlags_NoDecoration))
            return;
        auto frame = std::make_unique<platform::CustomWindowFrame>();
        if (!frame->Attach(native))
            return;
        // Removing the caption grew the client area; restore what ImGui asked for.
        frame->SetClientPosition(static_cast<int>(viewport->Pos.x), static_cast<int>(viewport->Pos.y));
        frame->SetClientSize(static_cast<int>(viewport->Size.x), static_cast<int>(viewport->Size.y));
        frame->SetAppearance(CurrentAppearance());
        m_frames[viewport->ID] = std::move(frame);
    }

    void EditorWindowChrome::OnDestroyWindow(ImGuiViewport *viewport)
    {
        // Destroying the window removes the frame's hook (WM_NCDESTROY), so
        // the caption is never restored on a window that is going away.
        m_backend.destroyWindow(viewport);
        m_frames.erase(viewport->ID);
    }

    // The backend positions windows through GLFW, which assumes a system
    // caption. Let it run first (it also suppresses the echo of its own move),
    // then correct the placement within the same frame so the echo of the
    // correction is suppressed as well.
    void EditorWindowChrome::OnSetWindowPos(ImGuiViewport *viewport, ImVec2 position)
    {
        m_backend.setWindowPos(viewport, position);
        if (auto *frame = FindFrame(viewport); frame && viewport != ImGui::GetMainViewport())
            frame->SetClientPosition(static_cast<int>(position.x), static_cast<int>(position.y));
    }

    void EditorWindowChrome::OnSetWindowSize(ImGuiViewport *viewport, ImVec2 size)
    {
        m_backend.setWindowSize(viewport, size);
        if (auto *frame = FindFrame(viewport); frame && viewport != ImGui::GetMainViewport())
            frame->SetClientSize(static_cast<int>(size.x), static_cast<int>(size.y));
    }
}
