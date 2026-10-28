#include "PlutoGE/ui/EditorTitleBar.h"

#include "PlutoGE/ui/EditorWindowChrome.h"

#include <imgui_internal.h>

#include <cmath>
#include <utility>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr const char *kBarWindowName = "##EditorTitleBar";
    }

    void EditorTitleBar::SetDocumentTitle(std::string title, bool modified)
    {
        m_title = std::move(title);
        m_modified = modified;
    }

    bool EditorTitleBar::BeginBarWindow()
    {
        ImGuiContext &g = *GImGui;
        auto *viewport = static_cast<ImGuiViewportP *>(ImGui::GetMainViewport());
        // As BeginMainMenuBar: makes GetFrameHeight() reflect the main viewport.
        ImGui::SetCurrentViewport(nullptr, viewport);
        if (!m_frame.begun)
        {
            m_frame.custom = m_chrome.IsEnabled();
            m_frame.metrics = caption::ComputeMetrics();
            m_frame.colors = caption::GetColors(caption::IsViewportFocused(viewport), m_tint);
        }

        // The menu row is centred in the taller caption, after the system menu button.
        const float frameHeight = ImGui::GetFrameHeight();
        const float height = m_frame.custom ? m_frame.metrics.height : frameHeight;
        const float leading = m_frame.custom ? height : g.Style.DisplaySafeAreaPadding.x;
        g.NextWindowData.MenuBarOffsetMinVal = ImVec2(leading, std::floor((height - frameHeight) * 0.5f));
        const ImVec4 background = ImGui::ColorConvertU32ToFloat4(m_frame.colors.background);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, background);
        ImGui::PushStyleColor(ImGuiCol_MenuBarBg, background);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        const bool open = ImGui::BeginViewportSideBar(kBarWindowName, viewport, ImGuiDir_Up, height,
                                                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar);
        g.NextWindowData.MenuBarOffsetMinVal = ImVec2(0.0f, 0.0f);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        if (!open)
        {
            ImGui::End();
            return false;
        }

        if (!m_frame.begun)
        {
            m_frame.begun = true;
            const ImGuiWindow *window = ImGui::GetCurrentWindow();
            const ImVec2 min = window->Pos;
            const ImVec2 max(min.x + window->Size.x, min.y + window->Size.y);
            m_frame.controlsLeft = m_frame.custom ? max.x - m_frame.metrics.buttonWidth * 3.0f : max.x;
            m_frame.layout.emplace(*window->Viewport);
            m_frame.layout->SetCaption(min, max);
            if (m_frame.custom)
                DrawSystemMenuButton();
        }
        return true;
    }

    void EditorTitleBar::DrawSystemMenuButton()
    {
        ImGuiWindow *window = ImGui::GetCurrentWindow();
        const float size = m_frame.metrics.height;
        const ImRect rect(window->Pos, ImVec2(window->Pos.x + size, window->Pos.y + size));
        ImGui::PushClipRect(rect.Min, rect.Max, false);
        const ImGuiID id = window->GetID("##SystemMenu");
        if (ImGui::ItemAdd(rect, id, nullptr, ImGuiItemFlags_NoNav))
        {
            bool hovered = false;
            bool held = false;
            const bool pressed = ImGui::ButtonBehavior(rect, id, &hovered, &held, ImGuiButtonFlags_PressedOnClick);
            if (held || hovered)
                window->DrawList->AddRectFilled(rect.Min, rect.Max,
                                                held ? m_frame.colors.buttonPressed : m_frame.colors.buttonHovered);
            caption::DrawAppMark(*window->DrawList, rect.GetCenter(), m_frame.metrics.glyphSize * 1.6f, m_frame.colors);
            m_frame.layout->AddInteractive(rect.Min, rect.Max);
            if (pressed)
                if (auto *frame = m_chrome.GetMainFrame())
                    frame->ShowSystemMenu(rect.Min.x - window->Viewport->Pos.x, rect.Max.y - window->Viewport->Pos.y);
        }
        ImGui::PopClipRect();
    }

    bool EditorTitleBar::BeginMenuBar()
    {
        if (!BeginBarWindow())
            return false;
        if (!ImGui::BeginMenuBar())
        {
            ImGui::End();
            return false;
        }
        // Menus and widgets stop short of the window controls.
        const ImGuiWindow *window = ImGui::GetCurrentWindow();
        ImGui::PushClipRect(window->Pos, ImVec2(m_frame.controlsLeft, window->Pos.y + window->Size.y), true);
        ImGui::BeginGroup();
        m_frame.inMenuBar = true;
        m_frame.inMenus = true;
        return true;
    }

    void EditorTitleBar::EndMenus()
    {
        if (!m_frame.inMenus)
            return;
        ImGui::EndGroup();
        m_frame.layout->AddLastItem();
        m_frame.inMenus = false;
        DrawDocumentTitle();
    }

    void EditorTitleBar::DrawDocumentTitle()
    {
        if (!m_frame.custom || m_title.empty())
            return;
        const ImGuiStyle &style = ImGui::GetStyle();
        const float padding = std::round(ImGui::GetFontSize() * 0.6f);
        const float dot = m_modified ? std::round(ImGui::GetFontSize() * 0.4f) : 0.0f;
        const ImVec2 textSize = ImGui::CalcTextSize(m_title.c_str());
        const float width = textSize.x + padding * 2.0f + (m_modified ? style.ItemInnerSpacing.x + dot : 0.0f);

        // Not an item that takes input, so the title drags the window like the rest of the caption.
        ImGui::SameLine(0.0f, style.ItemSpacing.x * 2.0f);
        ImGui::Dummy(ImVec2(width, ImGui::GetFrameHeight()));
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        ImDrawList &drawList = *ImGui::GetWindowDrawList();
        drawList.AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg), style.FrameRounding);
        const ImVec2 textPos(min.x + padding, std::floor(min.y + (max.y - min.y - textSize.y) * 0.5f));
        drawList.AddText(textPos, m_frame.colors.text, m_title.c_str());
        if (m_modified)
            drawList.AddCircleFilled(ImVec2(textPos.x + textSize.x + style.ItemInnerSpacing.x + dot * 0.5f,
                                            (min.y + max.y) * 0.5f),
                                     dot * 0.5f, m_frame.colors.accent);
    }

    void EditorTitleBar::AddInteractiveItem()
    {
        if (m_frame.layout)
            m_frame.layout->AddLastItem();
    }

    void EditorTitleBar::Status(std::string_view text)
    {
        if (!m_frame.inMenuBar || text.empty())
            return;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float right = m_frame.controlsLeft - spacing;
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        const std::string shown = caption::Elide(text, right - cursor.x - spacing);
        if (shown.empty())
            return;
        ImGui::SetCursorScreenPos(ImVec2(right - ImGui::CalcTextSize(shown.c_str()).x, cursor.y));
        ImGui::TextDisabled("%s", shown.c_str());
        // Only shortened text needs hover for its tooltip; otherwise it drags the window.
        if (shown.size() != text.size())
        {
            m_frame.layout->AddLastItem();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%.*s", static_cast<int>(text.size()), text.data());
        }
    }

    void EditorTitleBar::EndMenuBar()
    {
        if (!m_frame.inMenuBar)
            return;
        EndMenus();
        ImGui::PopClipRect();
        ImGui::EndMenuBar();
        m_frame.inMenuBar = false;

        // As EndMainMenuBar: when the user leaves the menus, focus returns to the window behind.
        ImGuiContext &g = *GImGui;
        if (g.CurrentWindow == g.NavWindow && g.NavLayer == ImGuiNavLayer_Main && !g.NavAnyRequest && g.ActiveId == 0)
            ImGui::FocusTopMostWindowUnderOne(g.NavWindow, nullptr, nullptr,
                                              ImGuiFocusRequestFlags_UnlessBelowModal |
                                                  ImGuiFocusRequestFlags_RestoreFocusedChild);
        ImGui::End();
    }

    void EditorTitleBar::Finish()
    {
        EndMenuBar();
        // With the system caption, frames that submitted no menus have no bar.
        if ((m_frame.begun || m_chrome.IsEnabled()) && BeginBarWindow())
        {
            auto *frame = m_frame.custom ? m_chrome.GetMainFrame() : nullptr;
            if (frame)
            {
                ImGuiWindow *window = ImGui::GetCurrentWindow();
                const ImVec2 min = window->Pos;
                const ImVec2 max(min.x + window->Size.x, min.y + window->Size.y);
                ImGui::PushClipRect(min, max, false);
                caption::WindowControls(*frame, ImVec2(max.x, min.y), m_frame.metrics, m_frame.colors, *m_frame.layout);
                window->DrawList->AddLine(ImVec2(min.x, max.y - 0.5f), ImVec2(max.x, max.y - 0.5f), m_frame.colors.border);
                ImGui::PopClipRect();
                m_frame.layout->Commit(*frame);
            }
            ImGui::End();
        }
        m_frame = {};
    }
}
