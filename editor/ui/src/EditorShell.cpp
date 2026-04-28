#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/panels/ViewportPanel.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <iostream>
#include <chrono>

namespace PlutoGE::ui
{
    void EditorShell::Initialize()
    {
        auto config = core::EngineConfig{
            platform::WindowConfig{
                .title = "PlutoGE Editor",
                .width = 1280,
                .height = 720,
                .resizable = true,
                .visible = true,
                .fullscreen = false,
            }};
        if (!m_engine.Initialize(config))
        {
            std::cerr << "Failed to initialize Engine in EditorShell" << std::endl;
        }

        m_panelManager.InitializeImGui(&m_engine.GetWindow());
        PanelConfig viewportConfig{"Viewport"};
        auto *viewportPanel = new ViewportPanel(viewportConfig);
        viewportPanel->Initialize();
        // m_panelManager.AddPanel(viewportPanel);
    }

    void EditorShell::Render()
    {
        auto &window = m_engine.GetWindow();
        auto &renderer = m_engine.GetRenderer();
        auto deltaTime = std::chrono::duration<float>::zero();
        auto lastTime = std::chrono::high_resolution_clock::now();

        while (!window.ShouldClose())
        {
            auto currentTime = std::chrono::high_resolution_clock::now();
            deltaTime = currentTime - lastTime;

            m_panelManager.BeginPanelUpdate();

            // ImGui::Begin("Main DockSpace", nullptr, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);

            // ImGui::DockSpace(ImGui::GetID("MainDockSpace"), ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
            // ImGui::End();

            // ImGui::Begin("Test");
            // ImGui::Text("Hello, world! Delta time: %.3f ms/frame (%.1f FPS)", deltaTime.count() * 1000.0f, 1.0f / deltaTime.count());
            // ImGui::End();

            renderer.BeginFrame();
            renderer.EndFrame();

            // ImGui::ShowStyleEditor();

            // // Show the ImGui demo window for debugging
            static bool showDemo = true;
            // ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
            // ImGui::SetNextWindowSize(ImVec2(400, 600), ImGuiCond_Always);
            ImGui::ShowMetricsWindow(&showDemo);
            // ImGui::ShowDemoWindow(&showDemo);

            // m_panelManager.UpdatePanels();
            m_panelManager.EndPanelUpdate();

            window.PollEvents();

            lastTime = currentTime;
        }
    }

    void EditorShell::Shutdown()
    {
        m_panelManager.ShutdownPanels();
        m_engine.Shutdown();
    }
}