#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/ui/SceneSnapshots.h"
#include <imgui.h>
#include <ctime>

namespace PlutoGE::ui
{
    void EditorShell::SaveRecoveryBackup()
    {
        if (!m_project || !m_scene || m_engine.IsRuntimeRunning() || m_activeBakeTask || m_sceneEditInProgress) return;
        std::string state;
        if (!CaptureSceneState(state, &m_recoveryError)) return;
        const auto identity = m_scene->GetFilePath() + '\0' + state;
        if (identity == m_lastRecoveryState) return;
        if (SceneRecovery::Save(m_recoveryDirectory, m_scene->GetFilePath(), state, m_recoverySettings.retainedBackups, m_recoveryError))
        {
            m_lastRecoveryState = identity;
            m_recoveryBackups = SceneRecovery::List(m_recoveryDirectory, m_recoveryScanErrors);
        }
        if (!m_recoveryError.empty()) Log(ConsoleSeverity::Warning, "Autosave: " + m_recoveryError);
    }

    void EditorShell::UpdateSceneRecovery()
    {
        const auto directory = m_project ? SceneRecovery::Directory(m_project->GetManifestPath()) : std::filesystem::path{};
        const auto now = std::chrono::steady_clock::now();
        if (directory != m_recoveryDirectory)
        {
            m_recoveryDirectory = directory;
            m_lastRecoveryState.clear();
            m_recoveryBackups.clear();
            m_recoveryScanErrors.clear();
            m_recoveryError.clear();
            m_showSceneRecovery = false;
            m_recoverySettings = {};
            if (!directory.empty())
            {
                if (!SceneRecovery::ReadSettings(directory, m_recoverySettings, m_recoveryError)) m_recoverySettings.enabled = false;
                m_recoveryBackups = SceneRecovery::List(directory, m_recoveryScanErrors);
                m_showSceneRecovery = !m_recoveryBackups.empty() || !m_recoveryScanErrors.empty() || !m_recoveryError.empty();
            }
            m_nextRecovery = now + std::chrono::seconds(m_recoverySettings.intervalSeconds);
        }
        if (directory.empty() || !m_recoverySettings.enabled || m_engine.IsRuntimeRunning() || m_activeBakeTask ||
            m_sceneEditInProgress || ImGui::IsAnyItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left)) return;
        if (now >= m_nextRecovery)
        {
            m_nextRecovery = now + std::chrono::seconds(m_recoverySettings.intervalSeconds);
            if (m_sceneDirty) SaveRecoveryBackup();
        }
    }

    void EditorShell::RenderSceneRecovery()
    {
        if (!m_showSceneRecovery || !m_project) return;
        ImGui::SetNextWindowSize(ImVec2(720, 460), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Autosave and Recovery", &m_showSceneRecovery)) { ImGui::End(); return; }
        ImGui::TextWrapped("Backups are separate from your scene files. Recovery opens an unsaved scene; review it and use Save As to keep it.");
        ImGui::Checkbox("Enable autosave", &m_recoverySettings.enabled);
        ImGui::InputInt("Interval (seconds, 10–3600)", &m_recoverySettings.intervalSeconds);
        ImGui::InputInt("Backups retained (1–50 per project)", &m_recoverySettings.retainedBackups);
        m_recoverySettings.intervalSeconds = std::clamp(m_recoverySettings.intervalSeconds, 10, 3600);
        m_recoverySettings.retainedBackups = std::clamp(m_recoverySettings.retainedBackups, 1, 50);
        if (ImGui::Button("Save Settings"))
        {
            if (SceneRecovery::WriteSettings(m_recoveryDirectory, m_recoverySettings, m_recoveryError))
                m_nextRecovery = std::chrono::steady_clock::now() + std::chrono::seconds(m_recoverySettings.intervalSeconds);
        }
        ImGui::SameLine();
        const bool busy = m_engine.IsRuntimeRunning() || m_activeBakeTask || m_sceneEditInProgress;
        ImGui::BeginDisabled(busy);
        if (ImGui::Button("Back Up Now")) SaveRecoveryBackup();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Refresh List")) m_recoveryBackups = SceneRecovery::List(m_recoveryDirectory, m_recoveryScanErrors);
        if (!m_recoveryError.empty()) ImGui::TextWrapped("%s", m_recoveryError.c_str());
        for (const auto &error : m_recoveryScanErrors) ImGui::TextWrapped("%s", error.c_str());
        ImGui::Separator();
        if (m_recoveryBackups.empty()) ImGui::TextUnformatted("No recovery backups in this project.");
        ImGui::BeginChild("Backups", ImVec2(0, 0));
        for (std::size_t i = 0; i < m_recoveryBackups.size(); ++i)
        {
            const auto &backup = m_recoveryBackups[i];
            ImGui::PushID(static_cast<int>(i));
            const auto seconds = static_cast<std::time_t>(backup.created / 1000000);
            char timestamp[64]{};
            if (const auto *local = std::localtime(&seconds)) std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", local);
            ImGui::Text("%s", timestamp);
            ImGui::TextWrapped("%s", backup.source.empty() ? "Untitled scene" : backup.source.c_str());
            ImGui::BeginDisabled(busy);
            if (ImGui::Button("Recover as Unsaved Scene"))
            {
                std::string state;
                if (SceneRecovery::Read(backup, state, m_recoveryError))
                {
                    auto restored = LoadSceneSnapshot(state, m_recoveryError);
                    if (restored && m_recoveryError.empty() && ConfirmContinueWithUnsavedChanges())
                    {
                        restored->SetFilePath({});
                        SetScene(std::move(restored), false);
                        m_recoveredSceneNeedsSaveAs = true;
                        m_undoStack.clear();
                        m_redoStack.clear();
                        m_savedSceneState.clear();
                        MarkSceneDirty();
                        m_statusMessage = "Recovered backup as an unsaved scene. Review before saving.";
                        m_showSceneRecovery = false;
                    }
                    else if (!restored && m_recoveryError.empty()) m_recoveryError = "Backup does not contain a valid scene.";
                }
            }
            ImGui::EndDisabled();
            ImGui::Separator();
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::End();
    }
}
