#pragma once

#include "PlutoGE/ui/ConsoleLog.h"

#include <filesystem>
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

    // The open project, so a command can open the file inside its project (IDE language services then load
    // the script project) instead of as a lone file. Either path may be empty, e.g. with no project open.
    struct ExternalEditorProjectContext
    {
        std::filesystem::path projectRoot; // {projectRoot}
        std::filesystem::path solution;    // {solution}
    };

    // Splits a command template into arguments (whitespace-separated, double quotes group) and substitutes
    // {file}, {line}, {column}, {projectRoot} and {solution} in each argument. An argument that only held
    // placeholders and expands to nothing is dropped, so "{projectRoot}" disappears when no project is open.
    // No shell is involved, so paths need no escaping.
    [[nodiscard]] std::vector<std::string> ExpandExternalEditorCommand(std::string_view commandTemplate,
                                                                       const ConsoleFileLocation &location,
                                                                       const ExternalEditorProjectContext &project = {});

    // Opens `location` with the configured editor, falling back to the system default when the command is
    // empty. Returns false with a reason when the editor could not be started.
    bool OpenInExternalScriptEditor(const ConsoleFileLocation &location,
                                    const ExternalEditorProjectContext &project,
                                    std::string *errorMessage = nullptr);

    // Preferences page body (ExternalScriptEditorUI.cpp).
    void RenderExternalToolsPreferences();
}
