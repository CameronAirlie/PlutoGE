#include "PlutoGE/ui/PanelManager.h"

#include "PlutoGE/ui/panels/Panel.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <ImGuizmo.h>

#include "PlutoGE/platform/Window.h"

#include <iostream>
#include <chrono>
#include <cfloat>
#include <algorithm>
#include <array>
#include <filesystem>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace PlutoGE::ui
{
    namespace
    {
        constexpr bool kEnableNativeMultiViewport = true;
        constexpr float kDefaultEditorFontSize = 15.0f;
        constexpr float kMinEditorFontSize = 10.0f;
        constexpr float kMaxEditorFontSize = 24.0f;

        float DurationMs(const std::chrono::high_resolution_clock::time_point &start,
                         const std::chrono::high_resolution_clock::time_point &end)
        {
            return std::chrono::duration<float, std::milli>(end - start).count();
        }

        std::filesystem::path GetExecutableDirectory()
        {
#ifdef _WIN32
            std::array<char, MAX_PATH> modulePath{};
            const DWORD modulePathLength = GetModuleFileNameA(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
            if (modulePathLength > 0 && modulePathLength < modulePath.size())
            {
                return std::filesystem::path(modulePath.data()).parent_path().lexically_normal();
            }
#endif
            return std::filesystem::current_path();
        }

        std::filesystem::path ResolveEditorFontPath()
        {
            constexpr const char *kMartianMonoFont = "resources/fonts/MartianMono-StdRg.ttf";
            const std::array<std::filesystem::path, 4> candidates = {
                GetExecutableDirectory() / kMartianMonoFont,
                std::filesystem::current_path() / kMartianMonoFont,
                std::filesystem::current_path() / "editor" / kMartianMonoFont,
                std::filesystem::current_path() / ".." / ".." / ".." / "editor" / kMartianMonoFont,
            };

            for (const auto &candidate : candidates)
            {
                std::error_code errorCode;
                if (std::filesystem::exists(candidate, errorCode))
                {
                    return candidate.lexically_normal();
                }
            }

            return {};
        }

        void LoadEditorFont(ImGuiIO &io)
        {
            const auto fontPath = ResolveEditorFontPath();
            if (!fontPath.empty())
            {
                ImFontConfig fontConfig{};
                fontConfig.OversampleH = 3;
                fontConfig.OversampleV = 2;
                fontConfig.PixelSnapH = false;
                if (io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), kDefaultEditorFontSize, &fontConfig))
                {
                    return;
                }
            }

            io.Fonts->AddFontDefault();
        }

        void ApplyModernProfessionalTheme()
        {
            ImGuiStyle &style = ImGui::GetStyle();
            style.WindowPadding = ImVec2(12.0f, 10.0f);
            style.FramePadding = ImVec2(10.0f, 6.0f);
            style.CellPadding = ImVec2(8.0f, 6.0f);
            style.ItemSpacing = ImVec2(8.0f, 6.0f);
            style.ItemInnerSpacing = ImVec2(6.0f, 5.0f);
            style.IndentSpacing = 18.0f;
            style.ScrollbarSize = 13.0f;
            style.GrabMinSize = 10.0f;

            style.WindowBorderSize = 1.0f;
            style.ChildBorderSize = 1.0f;
            style.PopupBorderSize = 1.0f;
            style.FrameBorderSize = 0.0f;
            style.TabBorderSize = 0.0f;

            style.WindowRounding = 6.0f;
            style.ChildRounding = 6.0f;
            style.FrameRounding = 4.0f;
            style.PopupRounding = 6.0f;
            style.ScrollbarRounding = 8.0f;
            style.GrabRounding = 4.0f;
            style.TabRounding = 4.0f;

            style.WindowMenuButtonPosition = ImGuiDir_Right;
            style.ColorButtonPosition = ImGuiDir_Right;
            style.SeparatorTextBorderSize = 1.0f;

            ImVec4 *colors = style.Colors;
            colors[ImGuiCol_Text] = ImVec4(0.88f, 0.91f, 0.94f, 1.00f);
            colors[ImGuiCol_TextDisabled] = ImVec4(0.48f, 0.53f, 0.58f, 1.00f);
            colors[ImGuiCol_WindowBg] = ImVec4(0.105f, 0.115f, 0.125f, 1.00f);
            colors[ImGuiCol_ChildBg] = ImVec4(0.085f, 0.095f, 0.105f, 1.00f);
            colors[ImGuiCol_PopupBg] = ImVec4(0.120f, 0.130f, 0.145f, 0.98f);
            colors[ImGuiCol_Border] = ImVec4(0.235f, 0.260f, 0.285f, 0.80f);
            colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
            colors[ImGuiCol_FrameBg] = ImVec4(0.160f, 0.175f, 0.190f, 1.00f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.205f, 0.235f, 0.260f, 1.00f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.245f, 0.305f, 0.350f, 1.00f);
            colors[ImGuiCol_TitleBg] = ImVec4(0.080f, 0.090f, 0.100f, 1.00f);
            colors[ImGuiCol_TitleBgActive] = ImVec4(0.105f, 0.120f, 0.135f, 1.00f);
            colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.080f, 0.090f, 0.100f, 0.95f);
            colors[ImGuiCol_MenuBarBg] = ImVec4(0.090f, 0.100f, 0.112f, 1.00f);
            colors[ImGuiCol_ScrollbarBg] = ImVec4(0.075f, 0.083f, 0.093f, 1.00f);
            colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.255f, 0.280f, 0.305f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.335f, 0.370f, 0.405f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.430f, 0.495f, 0.555f, 1.00f);
            colors[ImGuiCol_CheckMark] = ImVec4(0.390f, 0.720f, 0.760f, 1.00f);
            colors[ImGuiCol_SliderGrab] = ImVec4(0.345f, 0.650f, 0.700f, 1.00f);
            colors[ImGuiCol_SliderGrabActive] = ImVec4(0.500f, 0.800f, 0.830f, 1.00f);
            colors[ImGuiCol_Button] = ImVec4(0.175f, 0.195f, 0.215f, 1.00f);
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.235f, 0.290f, 0.320f, 1.00f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.300f, 0.405f, 0.440f, 1.00f);
            colors[ImGuiCol_Header] = ImVec4(0.180f, 0.235f, 0.260f, 1.00f);
            colors[ImGuiCol_HeaderHovered] = ImVec4(0.235f, 0.325f, 0.360f, 1.00f);
            colors[ImGuiCol_HeaderActive] = ImVec4(0.285f, 0.425f, 0.470f, 1.00f);
            colors[ImGuiCol_Separator] = ImVec4(0.250f, 0.275f, 0.300f, 0.90f);
            colors[ImGuiCol_SeparatorHovered] = ImVec4(0.360f, 0.600f, 0.640f, 1.00f);
            colors[ImGuiCol_SeparatorActive] = ImVec4(0.430f, 0.740f, 0.780f, 1.00f);
            colors[ImGuiCol_ResizeGrip] = ImVec4(0.300f, 0.440f, 0.480f, 0.35f);
            colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.430f, 0.700f, 0.740f, 0.70f);
            colors[ImGuiCol_ResizeGripActive] = ImVec4(0.520f, 0.820f, 0.850f, 0.95f);
            colors[ImGuiCol_Tab] = ImVec4(0.125f, 0.140f, 0.155f, 1.00f);
            colors[ImGuiCol_TabHovered] = ImVec4(0.235f, 0.325f, 0.360f, 1.00f);
            colors[ImGuiCol_TabActive] = ImVec4(0.180f, 0.235f, 0.260f, 1.00f);
            colors[ImGuiCol_TabUnfocused] = ImVec4(0.095f, 0.105f, 0.115f, 1.00f);
            colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.135f, 0.155f, 0.170f, 1.00f);
            colors[ImGuiCol_DockingPreview] = ImVec4(0.350f, 0.700f, 0.760f, 0.45f);
            colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.070f, 0.078f, 0.086f, 1.00f);
            colors[ImGuiCol_PlotLines] = ImVec4(0.600f, 0.670f, 0.720f, 1.00f);
            colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.450f, 0.780f, 0.820f, 1.00f);
            colors[ImGuiCol_PlotHistogram] = ImVec4(0.390f, 0.720f, 0.760f, 1.00f);
            colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.540f, 0.840f, 0.870f, 1.00f);
            colors[ImGuiCol_TableHeaderBg] = ImVec4(0.135f, 0.155f, 0.175f, 1.00f);
            colors[ImGuiCol_TableBorderStrong] = ImVec4(0.260f, 0.290f, 0.320f, 1.00f);
            colors[ImGuiCol_TableBorderLight] = ImVec4(0.190f, 0.210f, 0.235f, 1.00f);
            colors[ImGuiCol_TableRowBg] = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
            colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.000f, 1.000f, 1.000f, 0.035f);
            colors[ImGuiCol_TextSelectedBg] = ImVec4(0.330f, 0.650f, 0.700f, 0.35f);
            colors[ImGuiCol_DragDropTarget] = ImVec4(0.540f, 0.840f, 0.870f, 0.90f);
            colors[ImGuiCol_NavHighlight] = ImVec4(0.390f, 0.720f, 0.760f, 0.80f);
            colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.800f, 0.880f, 0.930f, 0.70f);
            colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.020f, 0.025f, 0.030f, 0.45f);
            colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.020f, 0.025f, 0.030f, 0.65f);
        }
    }

    bool PanelManager::InitializeImGui(platform::Window *window)
    {
        m_window = window;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // Enable Docking
        if (kEnableNativeMultiViewport)
        {
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
            io.ConfigViewportsNoDecoration = false;
        }
        LoadEditorFont(io);
        SetEditorFontSize(kDefaultEditorFontSize);

        ImGui_ImplGlfw_InitForOpenGL(static_cast<GLFWwindow *>(window->GetWindow()), true);
        ImGui_ImplOpenGL3_Init("#version 330 core");

        ApplyModernProfessionalTheme();

        ImGuiStyle &style = ImGui::GetStyle();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            style.WindowRounding = 6.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        return true;
    }

    void PanelManager::SetEditorFontSize(float fontSize)
    {
        m_editorFontSize = std::clamp(fontSize, kMinEditorFontSize, kMaxEditorFontSize);
        ImGui::GetIO().FontGlobalScale = m_editorFontSize / kDefaultEditorFontSize;
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
        auto &io = ImGui::GetIO();
        const bool suppressImguiMouse = m_window && m_window->IsCursorLocked();
        if (suppressImguiMouse)
        {
            io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        }
        else
        {
            io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        if (suppressImguiMouse)
        {
            io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
            io.MouseDelta = ImVec2(0.0f, 0.0f);
            io.MouseWheel = 0.0f;
            io.MouseWheelH = 0.0f;
            for (int buttonIndex = 0; buttonIndex < IM_ARRAYSIZE(io.MouseDown); ++buttonIndex)
            {
                io.MouseDown[buttonIndex] = false;
                io.MouseClicked[buttonIndex] = false;
                io.MouseReleased[buttonIndex] = false;
            }
        }
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
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
