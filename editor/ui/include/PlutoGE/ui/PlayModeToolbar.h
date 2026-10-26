#pragma once

#include <imgui.h>

#include <optional>

namespace PlutoGE::ui
{
    enum class PlayState
    {
        Editing,
        Playing,
        Paused,
    };

    // What the user asked for this frame; the host owns the actual transitions.
    struct PlayModeToolbarActions
    {
        bool play = false;
        bool togglePause = false;
        bool step = false;
        bool stop = false;
    };

    // Play-mode controls and the cues that make play mode hard to miss: a tinted
    // menu bar, centred Play/Pause/Step/Stop buttons and a framed game viewport.
    namespace PlayModeToolbar
    {
        // Accent for Playing (green) and Paused (amber); Editing has none.
        [[nodiscard]] std::optional<ImVec4> GetAccent(PlayState state);

        // Pushes a tinted ImGuiCol_MenuBarBg while playing or paused. Pass the
        // returned count to ImGui::PopStyleColor after BeginMainMenuBar.
        [[nodiscard]] int PushMenuBarTint(PlayState state);

        // Centred buttons drawn on the current menu bar, after its menus.
        PlayModeToolbarActions Draw(PlayState state, bool canPlay);

        // Outline plus a PLAYING / PAUSED badge drawn over a viewport image.
        void DrawViewportFrame(ImDrawList &drawList, ImVec2 min, ImVec2 max, PlayState state);
    }
}
