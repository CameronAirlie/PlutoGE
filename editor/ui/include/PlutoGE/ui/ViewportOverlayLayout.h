#pragma once
#include <imgui.h>
#include <string>

namespace PlutoGE::ui
{
    // Relative to the free space after subtracting the overlay's size.
    // This keeps moved overlays inside resized and detached viewports.
    struct ViewportOverlayLayout
    {
        ImVec2 fraction{};
        bool customized = false;
        ImVec2 grabOffset{};
        ImVec2 lastSize{};
    };
    void RegisterViewportOverlaySettings();
    ViewportOverlayLayout &GetViewportOverlayLayout(const std::string &name);
    void SaveViewportOverlayLayout();
}
