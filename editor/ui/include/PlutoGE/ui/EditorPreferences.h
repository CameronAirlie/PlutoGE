#pragma once

#include <string_view>

namespace PlutoGE::ui
{
    // EditorSettings keys for the Behaviour page.
    inline constexpr std::string_view kFocusGameViewOnPlayKey = "play.focusGameView";

    class EditorCommandRegistry;

    // Editor-wide (per user) preferences: theme, colours and interface chrome. The Keyboard page needs `commands`.
    void RenderEditorPreferences(bool *open, EditorCommandRegistry *commands = nullptr);
    // Shows the Keyboard page the next time Preferences is open.
    void SelectKeyboardPreferencesPage();
    // Menu body listing themes for quick switching.
    void RenderEditorThemeMenu();
}
