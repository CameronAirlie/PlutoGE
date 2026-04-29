#include "PlutoGE/ui/panels/ViewportPanel.h"
#include "PlutoGE/render/RenderTarget.h"
#include <iostream>

#include <imgui.h>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr int kDefaultViewportWidth = 1280;
        constexpr int kDefaultViewportHeight = 720;
        constexpr int kResizeDebounceFrames = 2;
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
        if (!m_renderTarget || !m_renderTarget->IsInitialized())
            return;

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