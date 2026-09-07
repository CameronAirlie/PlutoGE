#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/scene/SceneSerializer.h"

#include <imgui.h>

namespace PlutoGE::ui
{
    namespace
    {
        void RenderPropertyValue(const std::string &value)
        {
            // Serialized terrain/mesh data can be large. Keep review rows bounded
            // while retaining the complete value in the staged change.
            constexpr std::size_t previewLength = 240;
            if (value.size() <= previewLength)
                ImGui::TextWrapped("%s", value.c_str());
            else
            {
                const auto preview = value.substr(0, previewLength);
                ImGui::TextWrapped("%s... (%zu bytes)", preview.c_str(), value.size());
            }
        }
    }

    bool EditorShell::ApplyPlayModeChanges()
    {
        std::string beforeState;
        std::string error;
        if (!CaptureSceneState(beforeState, &error))
        {
            m_statusMessage = "Could not capture the scene: " + error;
            return false;
        }
        auto candidate = scene::SceneSerializer::LoadFromString(beforeState, &error);
        std::string afterState;
        if (!candidate || !PlayModeChanges::Apply(*candidate, m_playModeChanges, error) ||
            !scene::SceneSerializer::SaveToString(*candidate, afterState, &error))
        {
            m_statusMessage = "Could not keep play changes: " + error;
            Log(ConsoleSeverity::Error, m_statusMessage);
            return false;
        }
        candidate.reset();
        if (beforeState == afterState)
            return true;
        if (!RestoreSceneState(afterState, &error))
        {
            m_statusMessage = "Could not restore retained changes: " + error;
            Log(ConsoleSeverity::Error, m_statusMessage);
            return false;
        }
        PushSceneHistoryEntry(SceneHistoryEntry{.label = "Keep Play Mode Changes",
            .beforeState = std::move(beforeState), .afterState = std::move(afterState)});
        m_redoStack.clear();
        m_statusMessage = "Kept selected play-mode changes. Undo restores the pre-play values.";
        return true;
    }

    void EditorShell::RenderPlayModeChanges()
    {
        if (m_openPlayModeChanges)
        {
            ImGui::OpenPopup("Keep Play Mode Changes");
            m_openPlayModeChanges = false;
        }
        ImGui::SetNextWindowSize(ImVec2(850, 540), ImGuiCond_FirstUseEver);
        if (!ImGui::BeginPopupModal("Keep Play Mode Changes", nullptr))
            return;

        ImGui::TextWrapped("The original scene has been restored. Select the values to keep; Apply creates one undoable edit.");
        ImGui::TextWrapped("Includes serialized entity and component values. Spawned/deleted objects, changed component layouts, "
                           "and shared asset edits are excluded. Prefab overrides support the first component of each type.");
        if (ImGui::Button("Select available"))
            for (auto &change : m_playModeChanges)
                change.selected = change.unavailableReason.empty();
        ImGui::SameLine();
        if (ImGui::Button("Clear selection"))
            for (auto &change : m_playModeChanges)
                change.selected = false;

        ImGui::BeginChild("Changes", ImVec2(0, m_playModeChangesError.empty() ? -65 : -110), ImGuiChildFlags_Borders);
        if (m_playModeChanges.empty())
            ImGui::TextUnformatted("No supported property changes were found.");
        if (ImGui::BeginTable("Properties", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
        {
            ImGui::TableSetupColumn("Keep", ImGuiTableColumnFlags_WidthFixed, 38);
            ImGui::TableSetupColumn("Entity / property");
            ImGui::TableSetupColumn("Before play");
            ImGui::TableSetupColumn("At stop");
            ImGui::TableHeadersRow();
            for (std::size_t index = 0; index < m_playModeChanges.size(); ++index)
            {
                auto &change = m_playModeChanges[index];
                ImGui::PushID(static_cast<int>(index));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::BeginDisabled(!change.unavailableReason.empty());
                ImGui::Checkbox("##keep", &change.selected);
                ImGui::EndDisabled();
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s (#%u)", change.entityName.c_str(), change.entity);
                if (!change.componentName.empty())
                    ImGui::TextWrapped("%s [%zu]", change.componentName.c_str(), change.componentIndex + 1);
                ImGui::TextWrapped("%s", change.before.name == "$Enabled" ? "Enabled" : change.before.name.c_str());
                if (!change.unavailableReason.empty())
                    ImGui::TextWrapped("Unavailable: %s", change.unavailableReason.c_str());
                ImGui::TableNextColumn();
                RenderPropertyValue(change.before.value);
                ImGui::TableNextColumn();
                RenderPropertyValue(change.after.value);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();
        const auto selected = std::count_if(m_playModeChanges.begin(), m_playModeChanges.end(),
                                           [](const auto &change) { return change.selected; });
        ImGui::Text("%zu of %zu values selected", static_cast<std::size_t>(selected), m_playModeChanges.size());
        ImGui::BeginDisabled(selected == 0);
        if (ImGui::Button("Apply Selected"))
        {
            if (ApplyPlayModeChanges())
            {
                m_playModeChanges.clear();
                ImGui::CloseCurrentPopup();
            }
            else
                m_playModeChangesError = m_statusMessage;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Discard"))
        {
            m_playModeChanges.clear();
            ImGui::CloseCurrentPopup();
        }
        if (!m_playModeChangesError.empty())
            ImGui::TextWrapped("%s", m_playModeChangesError.c_str());
        ImGui::EndPopup();
    }
}
