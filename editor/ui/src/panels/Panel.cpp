#include "PlutoGE/ui/panels/Panel.h"
#include "PlutoGE/ui/EditorIcons.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <utility>

namespace PlutoGE::ui
{
    bool Panel::HasKeyboardFocus() const
    {
        if (!IsOpen() || !ImGui::GetCurrentContext())
            return false;
        auto *window = ImGui::FindWindowByName(GetHostWindowName().c_str());
        auto *focused = ImGui::GetCurrentContext()->NavWindow;
        return window && focused && ImGui::IsWindowChildOf(focused, window, true, false);
    }

    void Panel::Focus()
    {
        SetOpen(true);
        if (ImGui::GetCurrentContext())
            ImGui::SetWindowFocus(GetHostWindowName().c_str());
    }

    std::string Panel::GetHostWindowName() const
    {
        return m_isFullscreen ? "###PanelFullscreen" + m_config.name : m_config.name;
    }

    void Panel::SetFullscreen(bool fullscreen)
    {
        if (fullscreen == m_isFullscreen)
            return;
        m_isFullscreen = fullscreen;
        m_fullscreenChanged = true;
        if (fullscreen)
            m_isOpen = true;
    }

    namespace
    {
        bool IsHostedInMainViewport()
        {
            const ImGuiViewport *windowViewport = ImGui::GetWindowViewport();
            const ImGuiViewport *mainViewport = ImGui::GetMainViewport();
            return windowViewport != nullptr && mainViewport != nullptr && windowViewport->ID == mainViewport->ID;
        }

        bool DrawTitleBarButton(const char *id, const ImRect &rect, const char *label, const char *tooltip)
        {
            ImGuiWindow *window = ImGui::GetCurrentWindow();
            if (window == nullptr || window->SkipItems)
            {
                return false;
            }

            const ImGuiID buttonId = window->GetID(id);
            ImGui::KeepAliveID(buttonId);
            if (!ImGui::ItemAdd(rect, buttonId))
            {
                return false;
            }

            bool hovered = false;
            bool held = false;
            const bool pressed = ImGui::ButtonBehavior(rect, buttonId, &hovered, &held);

            if (hovered || held)
            {
                window->DrawList->AddRectFilled(rect.Min, rect.Max,
                                                ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered),
                                                ImGui::GetStyle().FrameRounding);
            }

            const ImVec2 textSize = ImGui::CalcTextSize(label);
            const ImVec2 textPos(
                rect.Min.x + (rect.GetWidth() - textSize.x) * 0.5f,
                rect.Min.y + (rect.GetHeight() - textSize.y) * 0.5f);
            window->DrawList->AddText(textPos, ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled), label);

            if (hovered && tooltip != nullptr && tooltip[0] != '\0')
            {
                ImGui::SetTooltip("%s", tooltip);
            }

            return pressed;
        }
    }

    bool Panel::BeginPanel()
    {
        if (m_isFullscreen)
            return BeginFullscreenPanel();
        // Restoring: the panel's own window reappears in its dock node; give it focus back.
        if (std::exchange(m_fullscreenChanged, false))
            ImGui::SetNextWindowFocus();
        const bool shouldRender = ImGui::Begin(m_config.name.c_str(), &m_isOpen, ImGuiWindowFlags_None);
        ApplyFloatingWindowState();
        DrawFloatingWindowControls();
        return shouldRender;
    }

    bool Panel::BeginFullscreenPanel()
    {
        // A separate window covers the work area, so the panel's docked window simply isn't
        // submitted meanwhile: ImGui keeps its dock node and tab for when it comes back.
        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        // Like the dockspace host it never jumps in front of its own overlay windows on click.
        const bool shouldRender = ImGui::Begin(GetHostWindowName().c_str(), nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar(3);

        m_fullscreenBeginOrder = -1;
        if (std::exchange(m_fullscreenChanged, false))
        {
            ImGuiWindow *window = ImGui::GetCurrentWindow();
            ImGui::BringWindowToDisplayFront(window);
            ImGui::FocusWindow(window);
            m_fullscreenBeginOrder = window->BeginOrderWithinContext;
        }
        return shouldRender;
    }

    void Panel::DrawFullscreenRestoreButton()
    {
        ImGuiWindow *window = ImGui::GetCurrentWindow();
        if (window == nullptr || window->SkipItems)
            return;

        // Revealed only when the pointer nears the top-right corner, so it never covers gameplay.
        const float buttonSize = ImGui::GetFrameHeight() * 1.25f;
        const float margin = ImGui::GetStyle().ItemSpacing.x;
        const ImVec2 max(window->Pos.x + window->Size.x - margin, window->Pos.y + margin + buttonSize);
        const ImVec2 min(max.x - buttonSize, max.y - buttonSize);
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const float revealDistance = buttonSize * 3.0f;
        if (!ImGui::IsMousePosValid(&mouse) || mouse.x < max.x - revealDistance || mouse.y > min.y + revealDistance ||
            !ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
            return;

        window->DrawList->AddRectFilled(min, max, IM_COL32(32, 33, 36, 210), ImGui::GetStyle().FrameRounding);
        if (DrawTitleBarButton("##PanelExitFullscreen", ImRect(min, max), icons::WindowRestore, "Exit fullscreen"))
            SetFullscreen(false);
    }

    void Panel::EndPanel()
    {
        if (m_isFullscreen)
        {
            DrawFullscreenRestoreButton();
            // On the frame it appears, windows the panel submitted from Render() (viewport
            // overlays and toolbars) go back above it, keeping their relative order.
            if (m_fullscreenBeginOrder >= 0)
            {
                ImGuiContext &g = *ImGui::GetCurrentContext();
                ImVector<ImGuiWindow *> submitted;
                for (ImGuiWindow *other : g.Windows)
                    if (other->Active && !(other->Flags & ImGuiWindowFlags_ChildWindow) &&
                        other->BeginOrderWithinContext > m_fullscreenBeginOrder)
                        submitted.push_back(other);
                std::sort(submitted.begin(), submitted.end(), [](const ImGuiWindow *a, const ImGuiWindow *b)
                          { return a->BeginOrderWithinContext < b->BeginOrderWithinContext; });
                for (ImGuiWindow *other : submitted)
                    ImGui::BringWindowToDisplayFront(other);
                m_fullscreenBeginOrder = -1;
            }
        }
        ImGui::End();
    }

    void Panel::ApplyFloatingWindowState()
    {
        if (ImGui::IsWindowDocked() || !IsHostedInMainViewport())
        {
            m_isMaximized = false;
            return;
        }

        if (!m_isMaximized)
        {
            return;
        }

        if (ImGuiViewport *viewport = ImGui::GetWindowViewport())
        {
            ImGui::SetWindowPos(viewport->WorkPos, ImGuiCond_Always);
            ImGui::SetWindowSize(viewport->WorkSize, ImGuiCond_Always);
        }
    }

    void Panel::DrawFloatingWindowControls()
    {
        if (ImGui::IsWindowDocked() || !IsHostedInMainViewport())
        {
            return;
        }

        ImGuiWindow *window = ImGui::GetCurrentWindow();
        if (window == nullptr || window->SkipItems)
        {
            return;
        }

        ImGuiStyle &style = ImGui::GetStyle();
        const ImRect titleBarRect = window->TitleBarRect();
        const float buttonHeight = ImMax(16.0f, titleBarRect.GetHeight() - style.FramePadding.y * 0.5f);
        const float buttonWidth = buttonHeight * 1.15f;
        const float spacing = style.ItemInnerSpacing.x;
        const float closeButtonReserve = titleBarRect.GetHeight();
        const float buttonTop = titleBarRect.Min.y + (titleBarRect.GetHeight() - buttonHeight) * 0.5f;
        const float maximizeRight = titleBarRect.Max.x - style.FramePadding.x - closeButtonReserve - spacing;
        const float maximizeLeft = maximizeRight - buttonWidth;
        const float minimizeRight = maximizeLeft - spacing;
        const float minimizeLeft = minimizeRight - buttonWidth;

        if (DrawTitleBarButton(
                "##PanelMinimize",
                ImRect(ImVec2(minimizeLeft, buttonTop), ImVec2(minimizeRight, buttonTop + buttonHeight)),
                icons::WindowMinimize,
                "Minimize"))
        {
            ImGui::SetWindowCollapsed(true, ImGuiCond_Always);
        }

        if (DrawTitleBarButton(
                "##PanelMaximize",
                ImRect(ImVec2(maximizeLeft, buttonTop), ImVec2(maximizeRight, buttonTop + buttonHeight)),
                m_isMaximized ? icons::WindowRestore : icons::WindowMaximize,
                m_isMaximized ? "Restore" : "Maximize"))
        {
            ToggleMaximized();
        }
    }

    void Panel::ToggleMaximized()
    {
        if (ImGui::IsWindowDocked() || !IsHostedInMainViewport())
        {
            return;
        }

        if (!m_isMaximized)
        {
            m_restorePos = ImGui::GetWindowPos();
            m_restoreSize = ImGui::GetWindowSize();
            m_isMaximized = true;
            return;
        }

        m_isMaximized = false;
        ImGui::SetWindowPos(m_restorePos, ImGuiCond_Always);
        ImGui::SetWindowSize(m_restoreSize, ImGuiCond_Always);
    }

    void Panel::Update()
    {
        if (!m_isOpen)
        {
            // Closing a fullscreen panel restores it, so reopening docks it where it was.
            m_isFullscreen = false;
            m_fullscreenChanged = false;
            m_wasVisibleLastFrame = false;
            return;
        }

        const float clampedAlpha = ImClamp(m_visualAlpha, 0.1f, 1.0f);
        if (clampedAlpha < 1.0f)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * clampedAlpha);
        }

        const bool shouldRender = BeginPanel();
        m_wasVisibleLastFrame = shouldRender;
        if (shouldRender)
        {
            if (!m_isInteractionEnabled)
            {
                ImGui::BeginDisabled();
            }
            Render();
            if (!m_isInteractionEnabled)
            {
                ImGui::EndDisabled();
            }
        }
        EndPanel();

        if (clampedAlpha < 1.0f)
        {
            ImGui::PopStyleVar();
        }
    }
}