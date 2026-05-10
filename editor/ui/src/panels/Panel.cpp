#pragma once

#include "PlutoGE/ui/panels/Panel.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace PlutoGE::ui
{
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

            const ImU32 backgroundColor = ImGui::GetColorU32(
                held ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered
                                                       : ImGuiCol_Button);
            window->DrawList->AddRectFilled(rect.Min, rect.Max, backgroundColor, 4.0f);

            const ImVec2 textSize = ImGui::CalcTextSize(label);
            const ImVec2 textPos(
                rect.Min.x + (rect.GetWidth() - textSize.x) * 0.5f,
                rect.Min.y + (rect.GetHeight() - textSize.y) * 0.5f);
            window->DrawList->AddText(textPos, ImGui::GetColorU32(ImGuiCol_Text), label);

            if (hovered && tooltip != nullptr && tooltip[0] != '\0')
            {
                ImGui::SetTooltip("%s", tooltip);
            }

            return pressed;
        }
    }

    bool Panel::BeginPanel()
    {
        const bool shouldRender = ImGui::Begin(m_config.name.c_str(), &m_isOpen, ImGuiWindowFlags_None);
        ApplyFloatingWindowState();
        DrawFloatingWindowControls();
        return shouldRender;
    }

    void Panel::EndPanel()
    {
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
                "_",
                "Minimize"))
        {
            ImGui::SetWindowCollapsed(true, ImGuiCond_Always);
        }

        if (DrawTitleBarButton(
                "##PanelMaximize",
                ImRect(ImVec2(maximizeLeft, buttonTop), ImVec2(maximizeRight, buttonTop + buttonHeight)),
                m_isMaximized ? "=" : "[]",
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
            m_wasVisibleLastFrame = false;
            return;
        }

        const bool shouldRender = BeginPanel();
        m_wasVisibleLastFrame = shouldRender;
        if (shouldRender)
        {
            Render();
        }
        EndPanel();
    }
}