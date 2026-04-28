#include "PlutoGE/ui/panels/ViewportPanel.h"
#include "PlutoGE/render/RenderTarget.h"
#include <iostream>
#include "PlutoGE/ui/EditorShell.h"

#include <imgui.h>

namespace PlutoGE::ui
{
    void ViewportPanel::Initialize()
    {
        auto renderConfig = render::RenderTargetConfig{
            .width = 1280,
            .height = 720,
            .clearColor = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f),
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

        // Get the available region size for the viewport
        ImVec2 panelSize = ImGui::GetContentRegionAvail();
        int newWidth = static_cast<int>(panelSize.x);
        int newHeight = static_cast<int>(panelSize.y);

        // Only resize if the size is valid and changed
        if (newWidth > 0 && newHeight > 0 && (newWidth != m_renderTarget->GetWidth() || newHeight != m_renderTarget->GetHeight()))
        {
            m_renderTarget->Cleanup();
            delete m_renderTarget;
            render::RenderTargetConfig renderConfig{
                .width = newWidth,
                .height = newHeight,
                .clearColor = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f),
            };
            m_renderTarget = new render::RenderTarget(renderConfig);
        }

        auto &renderer = EditorShell::GetInstance().GetEngine().GetRenderer();

        renderer.BeginFrame(m_renderTarget);
        renderer.DrawRenderTarget(m_renderTarget);
        renderer.EndFrame(m_renderTarget);

        // Draw the framebuffer texture to the ImGui panel
        ImTextureID texId = (ImTextureID)(uintptr_t)m_renderTarget->GetColorTextureID();
        ImVec2 imageSize = ImVec2((float)m_renderTarget->GetWidth(), (float)m_renderTarget->GetHeight());
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