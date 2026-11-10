#include "PlutoGE/ui/ExternalScriptEditor.h"

#include "PlutoGE/ui/EditorSettings.h"
#include "PlutoGE/ui/EditorWidgets.h"

#include <imgui.h>

#include <array>
#include <cstring>

namespace PlutoGE::ui
{
    void RenderExternalToolsPreferences()
    {
        auto &settings = EditorSettings::Get();
        widgets::PageTitle("External Tools", "Programs the editor hands files to. Saved for your user account, not the project.");

        widgets::SectionHeading("Script editor", "Opens C# files from the Console, compiler errors and script exceptions at the right line.");
        widgets::BeginCard("##ScriptEditor");
        if (widgets::BeginSettingsTable("##ScriptEditorTable"))
        {
            widgets::SettingsRow("Editor");
            const auto &current = FindExternalScriptEditorPreset(settings.GetString(kExternalEditorPresetKey, "system"));
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##preset", current.label))
            {
                for (const auto &preset : GetExternalScriptEditorPresets())
                {
                    const bool selected = &preset == &current;
                    if (ImGui::Selectable(preset.label, selected) && !selected)
                    {
                        // Seed a custom command from the editor being replaced, so it starts from something that works.
                        if (preset.command == nullptr && !settings.Has(kExternalEditorCommandKey) && current.command != nullptr)
                            settings.SetString(kExternalEditorCommandKey, current.command);
                        settings.SetString(kExternalEditorPresetKey, preset.id);
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            widgets::SettingsRow("Command", "{file}, {line}, {column}, {projectRoot} and {solution} are replaced. The program "
                                            "must be on PATH or a full path.");
            const bool custom = current.command == nullptr;
            std::array<char, 512> buffer{};
            const auto command = GetExternalScriptEditorCommand(settings);
            std::strncpy(buffer.data(), command.c_str(), buffer.size() - 1);
            ImGui::BeginDisabled(!custom);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputTextWithHint("##command", custom ? "editor --line {line} \"{file}\"" : "Opens with the default application",
                                         buffer.data(), buffer.size()) && custom)
                settings.SetString(kExternalEditorCommandKey, buffer.data());
            ImGui::EndDisabled();
            widgets::EndSettingsTable();
        }
        widgets::EndCard();
    }
}
