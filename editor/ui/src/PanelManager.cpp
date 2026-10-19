#include "PlutoGE/ui/PanelManager.h"
#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorTheme.h"
#include "PlutoGE/ui/ViewportOverlayLayout.h"
#include "PlutoGE/ui/panels/InspectorPanel.h"
#include "PlutoGE/core/CpuTrace.h"
#include "PlutoGE/ui/EditorCompositor.h"
#include "PlutoGE/ui/panels/ContentBrowserPanel.h"

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

        void MergeEditorIcons(ImGuiIO &io)
        {
            const auto path = ResolveEditorFontPath("fa-solid-900.ttf");
            if (path.empty())
            {
                std::cerr << "Editor icon font missing: fa-solid-900.ttf\n";
                return;
            }
            ImFontConfig config{};
            config.MergeMode = true;
            config.PixelSnapH = true;
            config.GlyphMinAdvanceX = kDefaultEditorFontSize;
            io.Fonts->AddFontFromFileTTF(path.string().c_str(), kDefaultEditorFontSize,
                                       &config, icons::Ranges);
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
                    auto *font = io.Fonts->Fonts.back();
                    MergeEditorIcons(io);
                    return font;
                }
            }

            return nullptr;
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

        // Layouts are saved per project by SetLayoutPath/SaveLayout. ImGui only
        // flags WantSaveIniSettings, so hidden or project-less sessions write nothing.
        io.IniFilename = nullptr;
        m_layoutLoadPending = true;

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
        ImFontConfig defaultFontConfig{};
        defaultFontConfig.SizePixels = kDefaultEditorFontSize;
        m_defaultFont = io.Fonts->AddFontDefault(&defaultFontConfig);
        MergeEditorIcons(io);
        RegisterViewportOverlaySettings();
        RegisterInspectorComponentSettings();
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

        LoadEditorThemeSettings();

        return true;
    }

    void PanelManager::ShutdownImGui()
    {
        if (ImGui::GetCurrentContext() == nullptr)
        {
            return;
        }

        SaveLayout();
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

    std::filesystem::path PanelManager::GetEditorFontPath(const char *fontFileName)
    { return ResolveEditorFontPath(fontFileName); }

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
            core::CpuScope panelScope(panel->GetName(), core::CpuCategory::UI);
            panel->Update();
            panelScope.End();
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

    void PanelManager::OnProjectChanged()
    {
        for (auto *panel : m_panels)
            panel->OnProjectChanged();
    }

    void PanelManager::SetLayoutPath(const std::filesystem::path &path)
    {
        if (path == m_layoutPath)
            return;
        SaveLayout();
        m_layoutPath = path;
        // Loading rebuilds dock nodes, so wait for the next frame boundary.
        m_layoutLoadPending = true;
    }

    void PanelManager::LoadLayout()
    {
        m_layoutLoadPending = false;
        ImGui::ClearIniSettings();
        std::error_code error;
        m_applyDefaultLayout = m_layoutPath.empty() || !std::filesystem::is_regular_file(m_layoutPath, error);
        if (!m_applyDefaultLayout)
            ImGui::LoadIniSettingsFromDisk(m_layoutPath.string().c_str());
    }

    void PanelManager::SaveLayout()
    {
        // A pending load means the live layout still belongs to the previous path.
        if (ImGui::GetCurrentContext() == nullptr || m_layoutPath.empty() || m_layoutLoadPending)
            return;
        std::error_code error;
        std::filesystem::create_directories(m_layoutPath.parent_path(), error);
        ImGui::SaveIniSettingsToDisk(m_layoutPath.string().c_str());
        ImGui::GetIO().WantSaveIniSettings = false;
    }

    void PanelManager::ShutdownPanels()
    {
        ClearCachedMaterialPreviews();
        for (auto panel : m_panels)
        {
            panel->Shutdown();
            delete panel;
        }
        m_panels.clear();
    }

    void PanelManager::BeginPanelUpdate(bool workspace)
    {
        core::CpuScope frameScope("ImGui frame startup", core::CpuCategory::UI);
        const auto beginPanelUpdateStart = std::chrono::high_resolution_clock::now();
        auto &io = ImGui::GetIO();
        // A locked cursor belongs to the game or the fly camera. GLFW keeps moving
        // its hidden virtual position, and the backend queues those events, so
        // writing io.MousePos here would be overwritten inside NewFrame. NoMouse
        // makes ImGui drop hover and clicks for the whole UI instead.
        const bool suppressImguiMouse = m_window && m_window->IsCursorLocked();
        constexpr ImGuiConfigFlags lockedMouseFlags = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange;
        if (suppressImguiMouse)
            io.ConfigFlags |= lockedMouseFlags;
        else
            io.ConfigFlags &= ~lockedMouseFlags;

        if (m_layoutLoadPending)
            LoadLayout();
        m_compositor->BeginFrame();
        { core::CpuScope scope("ImGui core new frame", core::CpuCategory::UI); ImGui::NewFrame(); }
        ImGuizmo::BeginFrame();
        const ImGuiID dockspaceId = workspace ? ImGui::DockSpaceOverViewport(ImGui::GetMainViewport()->ID) : 0;
        if (workspace && m_applyDefaultLayout)
        {
            BuildDefaultEditorLayout(dockspaceId);
            m_applyDefaultLayout = false;
        }
        m_timingStats.beginPanelUpdateMs = DurationMs(beginPanelUpdateStart, std::chrono::high_resolution_clock::now());
    }

    void PanelManager::PresentViewportOverlay(const EditorViewportRegion &region, EditorTextureHandle texture,
                                               bool bottomUp, const std::function<void()> &presentHost)
    {
        const auto extents = m_window->GetExtents();
        PresentEditorViewportOverlay(*m_compositor, region, static_cast<ImTextureID>(GetImGuiTextureId(texture)),
                                     bottomUp, extents.width, extents.height, presentHost);
    }

    void PanelManager::EndPanelUpdate()
    {
        const auto endPanelUpdateStart = std::chrono::high_resolution_clock::now();
        const auto imguiRenderStart = std::chrono::high_resolution_clock::now();
        ImGui::Render();
        m_compositor->RenderDrawData(ImGui::GetDrawData());
        const auto imguiRenderEnd = std::chrono::high_resolution_clock::now();
        m_timingStats.imguiRenderMs = DurationMs(imguiRenderStart, imguiRenderEnd);
        m_timingStats.platformViewportCount = ImGui::GetPlatformIO().Viewports.Size;

        ImGuiIO &io = ImGui::GetIO();
        if (io.WantSaveIniSettings)
        {
            SaveLayout();
            io.WantSaveIniSettings = false;
        }
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
