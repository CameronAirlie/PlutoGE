#include "PlutoGE/ui/ExternalScriptEditor.h"

#include "PlutoGE/ui/EditorSettings.h"
#include "PlutoGE/ui/SystemShell.h"


#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <utility>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr std::array<ExternalScriptEditorPreset, 5> kPresets{{
            {"system", "System default", ""},
            // Opening the project folder lets C# Dev Kit find the solution; a lone file gets no IntelliSense.
            {"vscode", "Visual Studio Code", "code \"{projectRoot}\" --goto \"{file}:{line}:{column}\""},
            {"visualstudio", "Visual Studio", "devenv /edit \"{file}\" /command \"Edit.GoTo {line}\""},
            {"rider", "JetBrains Rider", "rider --line {line} \"{file}\""},
            {"custom", "Custom", nullptr},
        }};

        void ReplaceAll(std::string &text, std::string_view token, std::string_view value)
        {
            for (std::size_t at = text.find(token); at != std::string::npos; at = text.find(token, at + value.size()))
                text.replace(at, token.size(), value);
        }

        std::string Utf8(const std::filesystem::path &path)
        {
            const auto text = path.u8string();
            return {reinterpret_cast<const char *>(text.data()), text.size()};
        }

        // Normalised, with the platform's separators; empty for an empty path.
        std::string PreferredUtf8(const std::filesystem::path &path)
        {
            return Utf8(path.lexically_normal().make_preferred());
        }
    }

    std::span<const ExternalScriptEditorPreset> GetExternalScriptEditorPresets()
    {
        return kPresets;
    }

    const ExternalScriptEditorPreset &FindExternalScriptEditorPreset(std::string_view id)
    {
        const auto it = std::find_if(kPresets.begin(), kPresets.end(), [&](const auto &preset) { return id == preset.id; });
        return it == kPresets.end() ? kPresets.front() : *it;
    }

    std::string GetExternalScriptEditorCommand(const EditorSettings &settings)
    {
        const auto &preset = FindExternalScriptEditorPreset(settings.GetString(kExternalEditorPresetKey, "system"));
        if (preset.command == nullptr)
            return settings.GetString(kExternalEditorCommandKey);
        return preset.command;
    }

    std::vector<std::string> ExpandExternalEditorCommand(std::string_view commandTemplate,
                                                         const ConsoleFileLocation &location,
                                                         const ExternalEditorProjectContext &project)
    {
        std::vector<std::string> arguments;
        std::string current;
        bool quoted = false;
        bool hasArgument = false;
        for (const char c : commandTemplate)
        {
            if (c == '"')
            {
                quoted = !quoted;
                hasArgument = true;
                continue;
            }
            if (!quoted && (c == ' ' || c == '\t'))
            {
                if (hasArgument)
                    arguments.push_back(std::exchange(current, {}));
                hasArgument = false;
                continue;
            }
            current += c;
            hasArgument = true;
        }
        if (hasArgument)
            arguments.push_back(std::move(current));

        const auto file = PreferredUtf8(location.file);
        const auto line = std::to_string(std::max(location.line, 1));
        const auto column = std::to_string(std::max(location.column, 1));
        const auto projectRoot = PreferredUtf8(project.projectRoot);
        const auto solution = PreferredUtf8(project.solution);
        std::vector<std::string> expanded;
        expanded.reserve(arguments.size());
        for (auto &argument : arguments)
        {
            const bool explicitlyEmpty = argument.empty(); // A literal "" is kept; see the header.
            ReplaceAll(argument, "{file}", file);
            ReplaceAll(argument, "{line}", line);
            ReplaceAll(argument, "{column}", column);
            ReplaceAll(argument, "{projectRoot}", projectRoot);
            ReplaceAll(argument, "{solution}", solution);
            if (explicitlyEmpty || !argument.empty())
                expanded.push_back(std::move(argument));
        }
        return expanded;
    }

    bool OpenInExternalScriptEditor(const ConsoleFileLocation &location,
                                    const ExternalEditorProjectContext &project,
                                    std::string *errorMessage)
    {
        if (!location.IsSet())
        {
            if (errorMessage)
                *errorMessage = "No source file to open.";
            return false;
        }
        const auto command = GetExternalScriptEditorCommand(EditorSettings::Get());
        const auto arguments = ExpandExternalEditorCommand(command, location, project);
        if (arguments.empty())
            return OpenWithSystem(location.file, errorMessage);
        if (LaunchDetached(arguments, errorMessage))
            return true;
        if (errorMessage)
            *errorMessage += " Check Preferences > External Tools.";
        return false;
    }
}
