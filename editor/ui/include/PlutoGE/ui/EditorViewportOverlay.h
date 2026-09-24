#pragma once
#include <imgui.h>
#include <functional>

namespace PlutoGE::ui
{
    class IEditorCompositor;

    // Coordinates belong to a completed ImGui frame, including its platform window.
    struct EditorViewportRegion
    {
        ImGuiID platformViewport = 0;
        ImVec2 min{};
        ImVec2 size{};
    };

    // Borrows completed draw lists; never edits the source frame or runs UI code.
    // The source frame and its textures must outlive this object. Hosts must not
    // begin another ImGui frame until presentation completes.
    class EditorViewportOverlay
    {
    public:
        EditorViewportOverlay(const ImDrawData &frame, const EditorViewportRegion &region,
                              ImTextureID texture, bool bottomUp);
        EditorViewportOverlay(const EditorViewportOverlay &) = delete;
        EditorViewportOverlay &operator=(const EditorViewportOverlay &) = delete;
        ImDrawData *GetDrawData() noexcept { return &m_frame; }
    private:
        ImDrawList m_overlay;
        ImDrawData m_frame;
    };

    // Replays all platform windows. presentHost must submit the main window
    // synchronously while the temporary draw data is alive. Layout stays frozen.
    void PresentEditorViewportOverlay(IEditorCompositor &compositor, const EditorViewportRegion &region,
                                      ImTextureID texture, bool bottomUp, int framebufferWidth, int framebufferHeight,
                                      const std::function<void()> &presentHost);
}
