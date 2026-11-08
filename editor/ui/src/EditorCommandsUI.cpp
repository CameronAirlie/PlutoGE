// Preferences > Keyboard: every editor command with its shortcut, click to record a new one.

#include "PlutoGE/ui/EditorCommands.h"

#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorTheme.h"
#include "PlutoGE/ui/EditorWidgets.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <string>

namespace PlutoGE::ui
{
    namespace
    {
        struct ShortcutPageState
        {
            std::string recording; // Command id waiting for a key press.
            std::array<char, 64> filter{};
        };

        ShortcutPageState &PageState()
        {
            static ShortcutPageState state;
            return state;
        }

        bool MatchesFilter(const EditorCommandDefinition &command, const std::string &shortcut, std::string_view filter)
        {
            return filter.empty() || FuzzyMatchScore(command.category + ": " + command.label, filter) >= 0 ||
                   FuzzyMatchScore(shortcut, filter) >= 0;
        }

        void ShortcutRow(EditorCommandRegistry &commands, const EditorCommandDefinition &command, const std::string &shortcut)
        {
            auto &state = PageState();
            ImGui::PushID(command.id.c_str());
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(command.label.c_str());
            if (command.alternateShortcut != ImGuiKey_None && !commands.IsCustomized(command.id))
            {
                ImGui::SameLine();
                ImGui::TextColored(GetEditorColor(EditorColor::TextMuted), "(also %s)", FormatShortcut(command.alternateShortcut).c_str());
            }

            ImGui::TableSetColumnIndex(1);
            const bool recording = state.recording == command.id;
            if (recording)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, GetEditorColor(EditorColor::Accent));
                ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::AccentText));
            }
            const std::string label = recording ? "Press keys... (Esc cancels)" : shortcut.empty() ? "Unassigned" : shortcut;
            if (ImGui::Button((label + "##shortcut").c_str(), ImVec2(-FLT_MIN, 0.0f)))
                state.recording = recording ? std::string{} : command.id;
            if (recording)
                ImGui::PopStyleColor(2);
            if (!recording)
                ImGui::SetItemTooltip("Click, then press the new shortcut.");
            if (const auto *other = commands.FindByShortcut(commands.GetShortcut(command.id), command.id); other && !recording)
                ImGui::TextColored(GetEditorColor(EditorColor::Warning), "%s Also %s: %s", icons::Warning, other->category.c_str(), other->label.c_str());

            ImGui::TableSetColumnIndex(2);
            if (commands.GetShortcut(command.id) != ImGuiKey_None && widgets::IconButton("##clear", icons::Close, "Remove the shortcut"))
                commands.SetShortcut(command.id, ImGuiKey_None);
            ImGui::TableSetColumnIndex(3);
            if (commands.IsCustomized(command.id) && widgets::IconButton("##reset", icons::Revert, ("Reset to " + (command.defaultShortcut == ImGuiKey_None ? std::string("none") : FormatShortcut(command.defaultShortcut))).c_str()))
                commands.ResetShortcut(command.id);
            ImGui::PopID();
        }
    }

    void RenderKeyboardShortcutsPage(EditorCommandRegistry &commands)
    {
        auto &state = PageState();
        widgets::PageTitle("Keyboard", "Shortcuts for editor commands. Saved for your user account. Ctrl+P finds any command by name.");

        // Record before drawing so the new shortcut shows this frame.
        if (!state.recording.empty())
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                state.recording.clear();
            else if (const auto chord = CapturePressedShortcut())
            {
                commands.SetShortcut(state.recording, *chord);
                state.recording.clear();
            }
        }
        if (!state.recording.empty())
            commands.MarkRecordingShortcut();

        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##ShortcutFilter", (std::string(icons::Search) + "  Filter commands or keys").c_str(), state.filter.data(),
                                 state.filter.size());
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        const std::string filter = state.filter.data();

        std::vector<std::string> categories;
        for (const auto &command : commands.List())
            if (std::find(categories.begin(), categories.end(), command.category) == categories.end())
                categories.push_back(command.category);

        for (const auto &category : categories)
        {
            bool any = false;
            for (const auto &command : commands.List())
                any = any || (command.category == category && MatchesFilter(command, commands.GetShortcutText(command.id), filter));
            if (!any)
                continue;
            widgets::SectionHeading(category.c_str());
            widgets::BeginCard(("##Keys" + category).c_str());
            if (ImGui::BeginTable(("##KeysTable" + category).c_str(), 4, ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Command", ImGuiTableColumnFlags_WidthStretch, 0.55f);
                ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthStretch, 0.45f);
                ImGui::TableSetupColumn("##clear", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
                ImGui::TableSetupColumn("##reset", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
                for (const auto &command : commands.List())
                {
                    const auto shortcut = commands.GetShortcutText(command.id);
                    if (command.category == category && MatchesFilter(command, shortcut, filter))
                        ShortcutRow(commands, command, shortcut);
                }
                ImGui::EndTable();
            }
            widgets::EndCard();
        }
    }
}
