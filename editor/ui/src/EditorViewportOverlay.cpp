#include "PlutoGE/ui/EditorViewportOverlay.h"
#include "PlutoGE/ui/EditorCompositor.h"
#include <memory>
#include <stdexcept>

namespace PlutoGE::ui
{
    EditorViewportOverlay::EditorViewportOverlay(const ImDrawData &frame, const EditorViewportRegion &region,
                                                 ImTextureID texture, bool bottomUp)
        : m_overlay(ImGui::GetDrawListSharedData()), m_frame(frame)
    {
        m_overlay.AddDrawCmd();
        const ImVec2 end(region.min.x + region.size.x, region.min.y + region.size.y);
        m_overlay.PushClipRect(region.min, end);
        m_overlay.AddImage(texture, region.min, end,
                           ImVec2(0, bottomUp ? 1.0f : 0.0f), ImVec2(1, bottomUp ? 0.0f : 1.0f));
        m_overlay.PopClipRect();
        m_frame.AddDrawList(&m_overlay);
    }
    void PresentEditorViewportOverlay(IEditorCompositor &compositor, const EditorViewportRegion &region,
                                      ImTextureID texture, bool bottomUp, int framebufferWidth, int framebufferHeight,
                                      const std::function<void()> &presentHost)
    {
        auto *mainViewport = ImGui::GetMainViewport();
        if (!mainViewport->DrawData || !mainViewport->DrawData->Valid)
            throw std::logic_error("Viewport loading requires a completed editor frame");
        auto *target = ImGui::FindViewportByID(region.platformViewport);
        std::unique_ptr<EditorViewportOverlay> overlay;
        ImDrawData *original = target ? target->DrawData : nullptr;
        if (original && original->Valid && region.size.x > 0 && region.size.y > 0 && texture != 0)
        {
            overlay = std::make_unique<EditorViewportOverlay>(*original, region,
                texture, bottomUp);
            target->DrawData = overlay->GetDrawData();
        }
        // Restore even if the backend throws; it must never retain a pointer to
        // our temporary draw data after this presentation scope.
        struct Restore
        {
            IEditorCompositor &compositor;
            ImGuiViewport *target;
            ImDrawData *original;
            ~Restore() { compositor.RenderDrawData(nullptr); if (target) target->DrawData = original; }
        } restore{compositor, target, original};
        // The native host can resize during loading. Keep frozen UI coordinates,
        // but clip them to the current framebuffer instead of stale extents.
        ImDrawData mainFrame = *mainViewport->DrawData;
        mainFrame.DisplaySize = ImVec2(framebufferWidth / mainFrame.FramebufferScale.x,
                                      framebufferHeight / mainFrame.FramebufferScale.y);
        compositor.RenderDrawData(&mainFrame);
        presentHost();
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
            compositor.RenderPlatformWindows(false);
    }
}
