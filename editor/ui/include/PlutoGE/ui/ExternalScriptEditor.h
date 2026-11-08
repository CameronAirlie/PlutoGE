#pragma once

#include "PlutoGE/ui/ConsoleLog.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::ui
{
    class EditorSettings;

    // A way of opening source files at a line. Presets cover common IDEs; Custom takes a user command.
    struct ExternalScriptEditorPreset
    {
        const char *id;        // Stored in settings.
        const char *label;     // Shown in Preferences.
        const char *command;   // Command template; empty means "open with the system default application".
    };

    // System default, VS Code, Visual Studio, Rider, Custom.
    [[nodiscard]] std::span<const ExternalScriptEditorPreset> GetExternalScriptEditorPresets();
    // The preset stored under `id`, or the system default when unknown.
    [[nodiscard]] const ExternalScriptEditorPreset &FindExternalScriptEditorPreset(std::string_view id);

    // Settings keys.
    inline constexpr std::string_view kExternalEditorPresetKey = "scripts.externalEditor";
    inline constexpr std::string_view kExternalEditorCommandKey = "scripts.externalEditorCommand";

    // The command template in effect: the preset's, or the user's for Custom.
    [[nodiscard]] std::string GetExternalScriptEditorCommand(const EditorSettings &settings);

    // Splits a command template into arguments (whitespace-separated, double quotes group) and substitutes
    // {file}, {line} and {column} in each argument. No shell is involved, so paths need no escaping.
    [[nodiscard]] std::vector<std::string> ExpandExternalEditorCommand(std::string_view commandTemplate,
                                                                       const ConsoleFileLocation &location);

    // Opens `location` with the configured editor, falling back to the system default when the command is
    // empty. Returns false with a reason when the editor could not be started.
    bool OpenInExternalScriptEditor(const ConsoleFileLocation &location, std::string *errorMessage = nullptr);

    // Preferences page body (ExternalScriptEditorUI.cpp).
    void RenderExternalToolsPreferences();
}
