#include "PlutoGE/ui/PanelManager.h"

#include "PlutoGE/ui/panels/Panel.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include "PlutoGE/platform/Window.h"

#include <iostream>
#include <chrono>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr bool kEnableNativeMultiViewport = true;

        float DurationMs(const std::chrono::high_resolution_clock::time_point &start,
                         const std::chrono::high_resolution_clock::time_point &end)
        {
            return std::chrono::duration<float, std::milli>(end - start).count();
        }
    }

    bool PanelManager::InitializeImGui(platform::Window *window)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // Enable Docking
        if (kEnableNativeMultiViewport)
        {
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        }

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
        ImGui::DockSpaceOverViewport(ImGui::GetMainViewport()->ID);
    }

    void PanelManager::EndPanelUpdate()
    {
        const auto endPanelUpdateStart = std::chrono::high_resolution_clock::now();
        const auto imguiRenderStart = std::chrono::high_resolution_clock::now();
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        const auto imguiRenderEnd = std::chrono::high_resolution_clock::now();
        m_timingStats.imguiRenderMs = DurationMs(imguiRenderStart, imguiRenderEnd);
        m_timingStats.platformViewportCount = ImGui::GetPlatformIO().Viewports.Size;

        ImGuiIO &io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            GLFWwindow *backup_current_context = glfwGetCurrentContext();

            const auto platformUpdateStart = std::chrono::high_resolution_clock::now();
            ImGui::UpdatePlatformWindows();
            const auto platformUpdateEnd = std::chrono::high_resolution_clock::now();

            const auto platformRenderStart = std::chrono::high_resolution_clock::now();
            ImGui::RenderPlatformWindowsDefault();
            const auto platformRenderEnd = std::chrono::high_resolution_clock::now();

            const auto contextRestoreStart = std::chrono::high_resolution_clock::now();
            glfwMakeContextCurrent(backup_current_context);
            const auto contextRestoreEnd = std::chrono::high_resolution_clock::now();

            m_timingStats.platformWindowsUpdateMs = DurationMs(platformUpdateStart, platformUpdateEnd);
            m_timingStats.platformWindowsRenderMs = DurationMs(platformRenderStart, platformRenderEnd);
            m_timingStats.contextRestoreMs = DurationMs(contextRestoreStart, contextRestoreEnd);
            m_timingStats.endPanelUpdateTotalMs = DurationMs(endPanelUpdateStart, contextRestoreEnd);
            return;
        }

        m_timingStats.platformWindowsUpdateMs = 0.0f;
        m_timingStats.platformWindowsRenderMs = 0.0f;
        m_timingStats.contextRestoreMs = 0.0f;
        m_timingStats.endPanelUpdateTotalMs = DurationMs(endPanelUpdateStart, std::chrono::high_resolution_clock::now());
    }
}