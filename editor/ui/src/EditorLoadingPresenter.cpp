#include "PlutoGE/ui/EditorLoadingPresenter.h"
#include "PlutoGE/ui/PanelManager.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/core/LoadingScreenSession.h"
#include "PlutoGE/render/Graphics.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace PlutoGE::ui
{
    EditorLoadingPresenter::EditorLoadingPresenter(core::Engine &engine, PanelManager &panels,
                                                   EditorViewportRegion region, render::LoadingScreenStyle style)
        : m_engine(engine), m_panels(panels), m_region(region), m_session(std::make_unique<core::LoadingScreenSession>(engine, std::move(style))),
          m_scriptInputEnabled(engine.GetWindow().IsScriptInputEnabled())
    {
        m_engine.GetWindow().SetScriptInputEnabled(false);
    }

    EditorLoadingPresenter::~EditorLoadingPresenter()
    {
        if (m_texture.IsValid()) m_panels.UnregisterTexture(m_texture);
        // Discard clicks/shortcuts collected while scene mutation was suspended.
        // They must not execute against the newly activated scene on resume.
        auto &io = ImGui::GetIO();
        io.ClearEventsQueue();
        io.ClearInputKeys();
        io.ClearInputMouse();
        m_engine.GetWindow().SetScriptInputEnabled(m_scriptInputEnabled);
    }

    void EditorLoadingPresenter::Present(const core::SceneLoadStatus &status)
    {
        auto &window = m_engine.GetWindow();
        window.PollEvents();
        if (window.ShouldClose()) return;
        const auto extents = window.GetExtents();
        if (extents.width <= 0 || extents.height <= 0) return;
        auto &service = m_engine.GetRhiRenderService();
        if (m_region.platformViewport && m_region.size.x > 0 && m_region.size.y > 0)
        {
            const auto *viewport = ImGui::FindViewportByID(m_region.platformViewport);
            const auto scale = viewport && viewport->DrawData ? viewport->DrawData->FramebufferScale : ImVec2(1, 1);
            const auto width = static_cast<std::uint32_t>(std::max(1.0f, std::ceil(m_region.size.x * scale.x)));
            const auto height = static_cast<std::uint32_t>(std::max(1.0f, std::ceil(m_region.size.y * scale.y)));
            if (!m_session->Render(status, width, height))
                throw std::runtime_error("Could not render viewport loading screen");
            const EditorTextureDescriptor descriptor{.device = m_engine.GetRenderDevice(),
                .texture = m_session->GetTexture(), .width = width, .height = height};
            if (!m_texture.IsValid())
            {
                m_texture = m_panels.RegisterTexture(descriptor);
                if (!m_texture.IsValid()) throw std::runtime_error("Could not register viewport loading texture");
            }
            else m_panels.UpdateTexture(m_texture, descriptor);
        }
        const bool vulkan = m_engine.GetConfig().graphicsApi == render::rhi::GraphicsApi::Vulkan;
        if (!vulkan)
        {
            // The RHI records raw GL state. Invalidate the legacy host cache
            // before rebinding the default framebuffer for editor composition.
            render::Graphics::ResetStateCache();
            render::Graphics::Disable(GL_SCISSOR_TEST);
            m_engine.GetRenderer().BeginFrame();
        }
        m_panels.PresentViewportOverlay(m_region, m_texture, true, [&]
        {
            if (vulkan)
            {
                if (!service.Present()) throw std::runtime_error("Could not present editor loading frame");
            }
            else m_engine.GetRenderer().EndFrame();
        });
    }
}
