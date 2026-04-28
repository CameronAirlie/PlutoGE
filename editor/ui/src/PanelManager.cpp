#include "PlutoGE/ui/PanelManager.h"

#include "PlutoGE/ui/panels/Panel.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include "PlutoGE/platform/Window.h"

#include <iostream>

namespace PlutoGE::ui
{
    bool PanelManager::InitializeImGui(platform::Window *window)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // Enable Docking
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;   // Enable Multi-Viewport / Platform Windows

        ImGui_ImplGlfw_InitForOpenGL(static_cast<GLFWwindow *>(window->GetWindow()), true);
        ImGui_ImplOpenGL3_Init("#version 330 core");

        // Setup Platform
        ImGuiStyle &style = ImGui::GetStyle();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        return true;
    }

    void PanelManager::AddPanel(Panel *panel)
    {
        m_panels.push_back(panel);
    }

    void PanelManager::UpdatePanels()
    {
        for (auto panel : m_panels)
        {
            panel->Update();
        }
    }

    void PanelManager::ShutdownPanels()
    {
        for (auto panel : m_panels)
        {
            panel->Shutdown();
            delete panel;
        }
        m_panels.clear();
    }

    void PanelManager::BeginPanelUpdate()
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void PanelManager::EndPanelUpdate()
    {
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        ImGuiIO &io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            GLFWwindow *backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);
        }
    }
}