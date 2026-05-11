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
            "Ambient Occlusion",
            "Global Illumination",
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
        for (auto &renderTarget : m_renderTargets)
        {
            renderTarget = new render::RenderTarget(renderConfig);
            if (!renderTarget->IsInitialized())
            {
                std::cerr << "Failed to initialize RenderTarget in ViewportPanel" << std::endl;
            }
        }

        m_displayRenderTargetIndex = 0;
        m_nextRenderTargetIndex = 1;
    }

    void ViewportPanel::Render()
    {
        m_isViewportHovered = false;
        m_isViewportFocused = false;

        auto *displayRenderTarget = GetDisplayedRenderTarget();
        if (!displayRenderTarget || !displayRenderTarget->IsInitialized())
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
        else if ((newWidth != displayRenderTarget->GetWidth() || newHeight != displayRenderTarget->GetHeight()) && ++m_resizeStableFrames >= kResizeDebounceFrames)
        {
            bool resized = true;
            for (auto *renderTarget : m_renderTargets)
            {
                if (renderTarget && !renderTarget->Resize(newWidth, newHeight))
                {
                    resized = false;
                    break;
                }
            }

            if (!resized)
            {
                std::cerr << "Failed to resize RenderTarget in ViewportPanel" << std::endl;
                return;
            }
        }

        ImTextureID texId = (ImTextureID)(uintptr_t)displayRenderTarget->GetColorTextureID();
        ImVec2 imageSize = ImVec2(static_cast<float>(displayRenderTarget->GetWidth()), static_cast<float>(displayRenderTarget->GetHeight()));
        ImGui::Image(texId, imageSize, ImVec2(0, 1), ImVec2(1, 0));
        m_isViewportHovered = ImGui::IsItemHovered();
        m_isViewportFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    }

    void ViewportPanel::RenderFrame(scene::CameraComponent &cameraComponent)
    {
        auto *renderTarget = GetRenderTargetForRender();
        if (!renderTarget || !renderTarget->IsInitialized())
            return;

        auto &renderer = EditorShell::GetInstance().GetEngine().GetRenderer();
        renderer.BeginFrame(renderTarget);
        auto lights = EditorShell::GetInstance().GetEngine().GetScene()->GetLights();
        renderer.RenderFrame(cameraComponent, renderTarget, lights);
        renderer.EndFrame(renderTarget);
        PresentRenderedFrame();
    }

    void ViewportPanel::RenderFrame(const render::CameraData &cameraData, std::vector<scene::Light *> lights, const std::vector<render::IPostProcessEffect *> *postProcessEffects)
    {
        auto *renderTarget = GetRenderTargetForRender();
        if (!renderTarget || !renderTarget->IsInitialized())
            return;

        auto &renderer = EditorShell::GetInstance().GetEngine().GetRenderer();
        renderer.BeginFrame(renderTarget);
        renderer.RenderFrame(cameraData, renderTarget, std::move(lights), postProcessEffects);
        renderer.EndFrame(renderTarget);
        PresentRenderedFrame();
    }

    bool ViewportPanel::ShouldRenderFrame() const
    {
        auto *renderTarget = GetRenderTargetForRender();
        return IsOpen() && WasVisibleLastFrame() && renderTarget && renderTarget->IsInitialized() && renderTarget->GetWidth() > 0 && renderTarget->GetHeight() > 0;
    }

    void ViewportPanel::ClearFrame()
    {
        auto *renderTarget = GetRenderTargetForRender();
        if (!renderTarget || !renderTarget->IsInitialized())
            return;

        auto &renderer = EditorShell::GetInstance().GetEngine().GetRenderer();
        renderer.BeginFrame(renderTarget);
        renderer.EndFrame(renderTarget);
        PresentRenderedFrame();
    }

    void ViewportPanel::Shutdown()
    {
        for (auto &renderTarget : m_renderTargets)
        {
            if (!renderTarget)
            {
                continue;
            }

            renderTarget->Cleanup();
            delete renderTarget;
            renderTarget = nullptr;
        }
    }

    render::RenderTarget *ViewportPanel::GetDisplayedRenderTarget() const
    {
        return m_renderTargets[m_displayRenderTargetIndex];
    }

    render::RenderTarget *ViewportPanel::GetRenderTargetForRender() const
    {
        return m_renderTargets[m_nextRenderTargetIndex];
    }

    void ViewportPanel::PresentRenderedFrame()
    {
        m_displayRenderTargetIndex = m_nextRenderTargetIndex;
        m_nextRenderTargetIndex = (m_displayRenderTargetIndex + 1) % static_cast<int>(m_renderTargets.size());
    }
}