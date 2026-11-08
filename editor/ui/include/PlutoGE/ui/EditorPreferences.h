#pragma once

#include <string_view>

namespace PlutoGE::ui
{
    // EditorSettings keys for the Behaviour page.
    inline constexpr std::string_view kFocusGameViewOnPlayKey = "play.focusGameView";

    // Editor-wide (per user) preferences: theme, colours and interface chrome.
    void RenderEditorPreferences(bool *open);
    // Menu body listing themes for quick switching.
    void RenderEditorThemeMenu();
}
