#pragma once

#include <imgui.h>

#include <span>

namespace PlutoGE::ui
{
    // Every content browser action that menus, shortcuts or toolbar buttons
    // can trigger. Add an action by appending an entry here and to the table
    // in ContentCommands.cpp, then handling it in the panel's Execute/CanExecute.
    enum class ContentCommand
    {
        Open,
        NewFolder,
        Rename,
        Cut,
        Copy,
        Paste,
        Duplicate,
        Delete,
        SelectAll,
        RevealInFileManager,
        OpenWithSystem,
        CopyPath,
        CopyReference,
        GoBack,
        GoForward,
        GoUp,
        Refresh,
    };

    struct ContentCommandInfo
    {
        ContentCommand command;
        const char *label;
        // Shortcut shown in menus and handled while the browser has focus;
        // ImGuiKey_None when there is none.
        ImGuiKeyChord chord;
        // Shown when the chord cannot be expressed (or as an alternative).
        const char *shortcutText;
    };

    [[nodiscard]] const ContentCommandInfo &GetContentCommandInfo(ContentCommand command);
    [[nodiscard]] std::span<const ContentCommandInfo> GetContentCommands();
    // Menu text for the command's shortcut, or null when it has none.
    [[nodiscard]] const char *GetContentCommandShortcut(ContentCommand command);
}
