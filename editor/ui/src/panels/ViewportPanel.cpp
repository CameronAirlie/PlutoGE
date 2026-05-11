#include "PlutoGE/ui/panels/ViewportPanel.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/render/Renderer.h"
#include <iostream>

#include <imgui.h>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr int kDefaultViewportWidth = 1280;
        constexpr int kDefaultViewportHeight = 720;
        constexpr int kResizeDebounceFrames = 2;
        constexpr const char *kDebugViewLabels[] = {
            "Post Process",
            "Quadrants",
            "Position",
            "Normal",
            "Albedo",
            "Depth",
        };
    }

    const char *ViewportPanel::GetDebugViewLabel(render::PostProcessDebugView debugView)
    {
        return kDebugViewLabels[static_cast<int>(debugView)];
    }

    void ViewportPanel::Initialize()
    {
        auto renderConfig = render::RenderTargetConfig{
            .width = kDefaultViewportWidth,
            .height = kDefaultViewportHeight,
            .clearColor = m_config.clearColor,
        };
        m_renderTarget = new render::RenderTarget(renderConfig);
        if (!m_renderTarget->IsInitialized())
        {
            std::cerr << "Failed to initialize RenderTarget in ViewportPanel" << std::endl;
        }
    }

    void ViewportPanel::Render()
    {
        m_isViewportHovered = false;
        m_isViewportFocused = false;

        if (!m_renderTarget || !m_renderTarget->IsInitialized())
            return;

        auto &renderer = EditorShell::GetInstance().GetEngine().GetRenderer();
        int debugView = static_cast<int>(renderer.GetPostProcessDebugView());
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::Combo("Debug View", &debugView, kDebugViewLabels, IM_ARRAYSIZE(kDebugViewLabels)))
        {
            renderer.SetPostProcessDebugView(static_cast<render::PostProcessDebugView>(debugView));
        }
        ImGui::Separator();

        const ImVec2 panelSize = ImGui::GetContentRegionAvail();
        const int newWidth = static_cast<int>(panelSize.x);
        const int newHeight = static_cast<int>(panelSize.y);

        if (newWidth <= 0 || newHeight <= 0)
        {
            return;
        }

        if (newWidth != m_pendingWidth || newHeight != m_pendingHeight)
        {
            m_pendingWidth = newWidth;
            m_pendingHeight = newHeight;
            m_resizeStableFrames = 0;
        }
        else if ((newWidth != m_renderTarget->GetWidth() || newHeight != m_renderTarget->GetHeight()) && ++m_resizeStableFrames >= kResizeDebounceFrames)
        {
            if (!m_renderTarget->Resize(newWidth, newHeight))
            {
                std::cerr << "Failed to resize RenderTarget in ViewportPanel" << std::endl;
                return;
            }
        }

        ImTextureID texId = (ImTextureID)(uintptr_t)m_renderTarget->GetColorTextureID();
        ImVec2 imageSize = ImVec2(static_cast<float>(m_renderTarget->GetWidth()), static_cast<float>(m_renderTarget->GetHeight()));
        ImGui::Image(texId, imageSize, ImVec2(0, 1), ImVec2(1, 0));
        m_isViewportHovered = ImGui::IsItemHovered();
        m_isViewportFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    }

    void ViewportPanel::RenderFrame(scene::CameraComponent &cameraComponent)
    {
        if (!m_renderTarget || !m_renderTarget->IsInitialized())
            return;

        auto &renderer = EditorShell::GetInstance().GetEngine().GetRenderer();
        renderer.BeginFrame(m_renderTarget);
        auto lights = EditorShell::GetInstance().GetEngine().GetScene()->GetLights();
        renderer.RenderFrame(cameraComponent, m_renderTarget, lights);
        renderer.EndFrame(m_renderTarget);
    }

    bool ViewportPanel::ShouldRenderFrame() const
    {
        return IsOpen() && WasVisibleLastFrame() && m_renderTarget && m_renderTarget->IsInitialized() && m_renderTarget->GetWidth() > 0 && m_renderTarget->GetHeight() > 0;
    }

    void ViewportPanel::ClearFrame()
    {
        if (!m_renderTarget || !m_renderTarget->IsInitialized())
            return;

        auto &renderer = EditorShell::GetInstance().GetEngine().GetRenderer();
        renderer.BeginFrame(m_renderTarget);
        renderer.EndFrame(m_renderTarget);
    }

    void ViewportPanel::Shutdown()
    {
        if (m_renderTarget)
        {
            m_renderTarget->Cleanup();
            delete m_renderTarget;
            m_renderTarget = nullptr;
        }
    }
}