#pragma once

#include <array>

namespace PlutoGE::ui
{
    class EditorSettings;

    // Editor Viewport tool choices kept between sessions, per user: the gizmo, its space, snapping and the grid.
    struct ViewportToolPreferences
    {
        enum class Tool
        {
            Move,
            Rotate,
            Scale,
        };

        Tool tool = Tool::Move;
        bool localSpace = true;
        bool snap = false;
        std::array<float, 3> moveSnap{1.0f, 1.0f, 1.0f};
        float rotateSnapDegrees = 15.0f;
        float scaleSnap = 0.1f;
        bool showGrid = true;

        bool operator==(const ViewportToolPreferences &) const = default;
    };

    // Missing or out-of-range values keep the defaults' value.
    [[nodiscard]] ViewportToolPreferences LoadViewportToolPreferences(const EditorSettings &settings,
                                                                      const ViewportToolPreferences &defaults = {});
    void SaveViewportToolPreferences(EditorSettings &settings, const ViewportToolPreferences &preferences);

    // Holding Ctrl while dragging a gizmo turns snapping on, or off when it is already on.
    [[nodiscard]] constexpr bool IsSnapActive(bool snapEnabled, bool ctrlHeld) { return snapEnabled != ctrlHeld; }
}
