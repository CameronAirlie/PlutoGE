#include "PlutoGE/ui/PanelManager.h"
#include "PlutoGE/ui/EditorCompositor.h"

#include "PlutoGE/ui/panels/Panel.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <ImGuizmo.h>

#include "PlutoGE/platform/Window.h"

#include <iostream>
#include <chrono>
#include <cfloat>
#include <algorithm>
#include <array>
#include <cstdlib>
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
    PanelManager::PanelManager() = default;
    PanelManager::~PanelManager() = default;

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

        std::filesystem::path GetEditorSettingsDirectory()
        {
#ifdef _WIN32
            if (const char *appData = std::getenv("APPDATA"); appData != nullptr && appData[0] != '\0')
            {
                return std::filesystem::path(appData) / "PlutoGE";
            }
#else
            if (const char *configHome = std::getenv("XDG_CONFIG_HOME"); configHome != nullptr && configHome[0] != '\0')
            {
                return std::filesystem::path(configHome) / "PlutoGE";
            }
            if (const char *home = std::getenv("HOME"); home != nullptr && home[0] != '\0')
            {
                return std::filesystem::path(home) / ".config" / "PlutoGE";
            }
#endif
            return GetExecutableDirectory();
        }

        void BuildDefaultEditorLayout(ImGuiID dockspaceId)
        {
            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->WorkSize);

            ImGuiID centerId = dockspaceId;
            const ImGuiID leftId = ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Left, 0.20f, nullptr, &centerId);
            const ImGuiID rightId = ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Right, 0.25f, nullptr, &centerId);
            const ImGuiID bottomId = ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Down, 0.28f, nullptr, &centerId);

            ImGui::DockBuilderDockWindow("Scene Hierarchy", leftId);
            ImGui::DockBuilderDockWindow("Inspector", rightId);
            ImGui::DockBuilderDockWindow("Editor Viewport", centerId);
            ImGui::DockBuilderDockWindow("Game Viewport", centerId);
            ImGui::DockBuilderDockWindow("Content Browser", bottomId);
            ImGui::DockBuilderDockWindow("Console", bottomId);
            ImGui::DockBuilderFinish(dockspaceId);
        }

        std::filesystem::path ResolveEditorFontPath(const char *fontFileName)
        {
            const std::filesystem::path relativeFontPath = std::filesystem::path("resources/fonts") / fontFileName;
            const std::array<std::filesystem::path, 4> candidates = {
                GetExecutableDirectory() / relativeFontPath,
                std::filesystem::current_path() / relativeFontPath,
                std::filesystem::current_path() / "editor" / relativeFontPath,
                std::filesystem::current_path() / ".." / ".." / ".." / "editor" / relativeFontPath,
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

        ImFont *LoadEditorFont(ImGuiIO &io, const char *fontFileName)
        {
            const auto fontPath = ResolveEditorFontPath(fontFileName);
            if (!fontPath.empty())
            {
                ImFontConfig fontConfig{};
                fontConfig.OversampleH = 3;
                fontConfig.OversampleV = 2;
                fontConfig.PixelSnapH = true;
                if (io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), kDefaultEditorFontSize, &fontConfig))
                {
                    return io.Fonts->Fonts.back();
                }
            }

            return nullptr;
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

    bool PanelManager::InitializeImGui(platform::Window *window,
                                       render::rhi::IRenderDevice *device,
                                       render::rhi::ISwapchain *swapchain)
    {
        if (!window || !device || !swapchain)
            return false;
        m_window = window;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();

        const std::filesystem::path settingsDirectory = GetEditorSettingsDirectory();
        std::error_code settingsError;
        std::filesystem::create_directories(settingsDirectory, settingsError);
        const std::filesystem::path iniPath = settingsDirectory / "editor-layout.ini";
        m_applyDefaultLayout = !std::filesystem::exists(iniPath, settingsError);
        m_imguiIniPath = iniPath.string();
        io.IniFilename = m_imguiIniPath.c_str();

        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // Enable Docking
        if (kEnableNativeMultiViewport)
        {
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
            io.ConfigViewportsNoDecoration = false;
        }
        m_martianMonoFont = LoadEditorFont(io, "MartianMono-StdRg.ttf");
        m_georamaFont = LoadEditorFont(io, "Georama-Regular.ttf");
        m_defaultFont = io.Fonts->AddFontDefault();
        SetEditorFont(m_editorFont);
        SetEditorFontSize(kDefaultEditorFontSize);

        m_compositor = CreateEditorCompositor(device->GetApi());
        if (!m_compositor || !m_compositor->Initialize(*window, *device, *swapchain))
        {
            m_compositor.reset();
            ImGui::DestroyContext();
            m_window = nullptr;
            return false;
        }

        ApplyModernProfessionalTheme();

        ImGuiStyle &style = ImGui::GetStyle();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            style.WindowRounding = 6.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        return true;
    }

    void PanelManager::ShutdownImGui()
    {
        if (ImGui::GetCurrentContext() == nullptr)
        {
            return;
        }

        ImGui::SaveIniSettingsToDisk(m_imguiIniPath.c_str());
        if (m_compositor)
            m_compositor->Shutdown();
        m_compositor.reset();
        ImGui::DestroyContext();
        m_window = nullptr;
    }

    EditorTextureHandle PanelManager::RegisterTexture(const EditorTextureDescriptor &descriptor)
    {
        return m_compositor ? m_compositor->RegisterTexture(descriptor) : EditorTextureHandle{};
    }

    void PanelManager::UpdateTexture(EditorTextureHandle texture, const EditorTextureDescriptor &descriptor)
    {
        if (m_compositor)
            m_compositor->UpdateTexture(texture, descriptor);
    }

    void PanelManager::UnregisterTexture(EditorTextureHandle texture)
    {
        if (m_compositor)
            m_compositor->UnregisterTexture(texture);
    }

    std::uint64_t PanelManager::GetImGuiTextureId(EditorTextureHandle texture) const noexcept
    {
        return m_compositor ? m_compositor->GetImGuiTextureId(texture) : 0;
    }

    void PanelManager::SetEditorFontSize(float fontSize)
    {
        m_editorFontSize = std::clamp(fontSize, kMinEditorFontSize, kMaxEditorFontSize);
        ImGui::GetIO().FontGlobalScale = m_editorFontSize / kDefaultEditorFontSize;
    }

    void PanelManager::SetEditorFont(const std::string &fontName)
    {
        ImFont *selectedFont = nullptr;
        if (fontName == "Georama")
        {
            selectedFont = m_georamaFont;
        }
        else if (fontName == "ImGui Default")
        {
            selectedFont = m_defaultFont;
        }
        else
        {
            selectedFont = m_martianMonoFont;
        }

        if (selectedFont == nullptr)
        {
            selectedFont = m_defaultFont;
        }
        ImGui::GetIO().FontDefault = selectedFont;
        m_editorFont = fontName == "Georama" || fontName == "ImGui Default" ? fontName : "Martian Mono";
    }

    void PanelManager::AddPanel(Panel *panel)
    {
        m_panels.push_back(panel);
    }

    void PanelManager::UpdatePanels()
    {
        std::vector<PanelUpdateTiming> completedTimings;
        completedTimings.reserve(m_panels.size());
        float totalMs = 0.0f;
        for (auto panel : m_panels)
        {
            const auto panelStart = std::chrono::high_resolution_clock::now();
            panel->Update();
            const auto panelEnd = std::chrono::high_resolution_clock::now();
            const float updateMs = DurationMs(panelStart, panelEnd);
            totalMs += updateMs;
            completedTimings.push_back({panel->GetName(), updateMs, panel->IsOpen(), panel->WasVisibleLastFrame()});
        }

        // Publish only after every panel has finished. The profiler panel reads the
        // previous completed frame while this frame's measurements are collected.
        m_timingStats.panelUpdatesTotalMs = totalMs;
        m_timingStats.panelUpdates = std::move(completedTimings);
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
        const auto beginPanelUpdateStart = std::chrono::high_resolution_clock::now();
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

        m_compositor->BeginFrame();
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
        const ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(ImGui::GetMainViewport()->ID);
        if (m_applyDefaultLayout)
        {
            BuildDefaultEditorLayout(dockspaceId);
            m_applyDefaultLayout = false;
        }
        m_timingStats.beginPanelUpdateMs = DurationMs(beginPanelUpdateStart, std::chrono::high_resolution_clock::now());
    }

    void PanelManager::EndPanelUpdate()
    {
        const auto endPanelUpdateStart = std::chrono::high_resolution_clock::now();
        const auto imguiRenderStart = std::chrono::high_resolution_clock::now();
        ImGui::Render();
        m_compositor->RenderDrawData();
        const auto imguiRenderEnd = std::chrono::high_resolution_clock::now();
        m_timingStats.imguiRenderMs = DurationMs(imguiRenderStart, imguiRenderEnd);
        m_timingStats.platformViewportCount = ImGui::GetPlatformIO().Viewports.Size;

        ImGuiIO &io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            const auto platformUpdateStart = std::chrono::high_resolution_clock::now();
            m_compositor->RenderPlatformWindows();
            const auto platformUpdateEnd = std::chrono::high_resolution_clock::now();

            const auto platformRenderStart = std::chrono::high_resolution_clock::now();
            const auto platformRenderEnd = std::chrono::high_resolution_clock::now();

            const auto contextRestoreStart = std::chrono::high_resolution_clock::now();
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
