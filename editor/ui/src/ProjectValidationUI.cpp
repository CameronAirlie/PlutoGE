#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scripting/ScriptEngine.h"
#include <imgui.h>

namespace PlutoGE::ui
{
    namespace
    {
        std::string CurrentOwner(const assets::Project &project, const scene::Scene &scene)
        {
            if (scene.GetFilePath().empty()) return "Current unsaved scene";
            if (!project.IsInAssetDirectory(scene.GetFilePath())) return "Current external scene";
            return project.MakeAssetReference(scene.GetFilePath());
        }
    }
    bool EditorShell::RunProjectValidation(bool includeCurrentScene)
    {
        if (!m_project || m_engine.IsRuntimeRunning() || m_activeBakeTask) return false;
        m_validationProject = m_project->GetManifestPath();
        m_validationResult = {};
        m_validationHasRun = true;
        m_validationCurrentOwner.clear();
        m_validationCurrentState.clear();
        assets::ProjectValidationInput input;
        input.assetRoot = m_project->GetAssetDirectoryPath();
        input.startupScene = m_project->GetManifest().startupScene;
        if (!m_project->GetManifest().scriptAssembly.empty()) input.scriptAssembly = ResolveProjectScriptAssemblyPath();
        const auto builtins = assets::Project::GetBuiltinAssetReferences();
        input.builtinReferences.insert(builtins.begin(), builtins.end());
        input.resolveEngineReference = [&](const auto &reference) { return m_engine.GetAssetManager().ResolveAssetPath(reference); };
        const auto &scripts = m_engine.GetScriptEngine();
        // Never use classes left over from a different project/assembly.
        if (!input.scriptAssembly.empty() && scripts.GetAssemblyPath().lexically_normal() == input.scriptAssembly.lexically_normal() && scripts.GetLastError().empty())
        {
            const auto names = scripts.GetClassNames();
            input.scriptClasses.emplace(names.begin(), names.end());
        }
        if (includeCurrentScene && m_scene)
        {
            std::string state, error;
            if (!CaptureSceneState(state, &error))
            {
                m_validationResult.diagnostics.push_back({assets::ValidationSeverity::Error, "scan.snapshot", "Current scene", 0, 0, error});
                return false;
            }
            input.currentSceneOwner = CurrentOwner(*m_project, *m_scene);
            m_validationCurrentOwner = input.currentSceneOwner;
            m_validationCurrentState = state;
            input.currentScene = std::move(state);
        }
        try { m_validationResult = assets::ValidateProject(input); }
        catch (const std::exception &error)
        { m_validationResult.diagnostics.push_back({assets::ValidationSeverity::Error, "scan.failed", "Project", 0, 0, error.what()}); }
        return !m_validationResult.HasErrors();
    }

    void EditorShell::RenderProjectValidation(const std::function<void(const std::string &)> &reveal)
    {
        if (!m_project || m_validationProject != m_project->GetManifestPath())
        {
            m_validationResult = {};
            m_validationHasRun = false;
            m_validationCurrentOwner.clear();
            m_validationCurrentState.clear();
            m_validationProject = m_project ? m_project->GetManifestPath() : std::filesystem::path{};
        }
        if (!m_showProjectValidation || !m_project) return;
        ImGui::SetNextWindowSize(ImVec2(850, 500), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Project Validation", &m_showProjectValidation)) { ImGui::End(); return; }
        const bool busy = m_engine.IsRuntimeRunning() || m_activeBakeTask || m_sceneEditInProgress;
        ImGui::BeginDisabled(busy);
        if (ImGui::Button("Validate Project")) RunProjectValidation(true);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Checkbox("Show warnings", &m_validationWarnings);
        ImGui::TextWrapped("Checks saved assets and the current scene. Results are a snapshot: validate again after edits or imports. Export validates saved data again and stops on errors.");
        if (!m_validationHasRun) ImGui::TextUnformatted("Choose Validate Project to run checks.");
        else
        {
            const auto errors = std::count_if(m_validationResult.diagnostics.begin(), m_validationResult.diagnostics.end(), [](const auto &d) { return d.severity == assets::ValidationSeverity::Error; });
            ImGui::Text("%zu files checked; %zu errors, %zu warnings", m_validationResult.checkedFiles,
                        static_cast<std::size_t>(errors), m_validationResult.diagnostics.size() - static_cast<std::size_t>(errors));
            if (m_validationResult.diagnostics.empty()) ImGui::TextUnformatted("No issues found by the available checks.");
        }
        ImGui::BeginChild("Diagnostics");
        for (std::size_t i = 0; i < m_validationResult.diagnostics.size(); ++i)
        {
            const auto &d = m_validationResult.diagnostics[i];
            if (!m_validationWarnings && d.severity == assets::ValidationSeverity::Warning) continue;
            ImGui::PushID(static_cast<int>(i));
            ImGui::Text("%s [%s]", d.severity == assets::ValidationSeverity::Error ? "Error" : "Warning", d.code.c_str());
            ImGui::TextWrapped("%s", d.message.c_str());
            ImGui::TextWrapped("%s | Entity %u | Line %zu", d.owner.c_str(), d.entity, d.line);
            ImGui::BeginDisabled(busy);
            if (d.owner.starts_with("project://"))
            {
                if (ImGui::SmallButton("Show Asset")) reveal(d.owner);
                ImGui::SameLine();
            }
            if (d.entity && ImGui::SmallButton("Select Entity"))
            {
                bool current = false;
                if (m_scene)
                {
                    const auto owner = CurrentOwner(*m_project, *m_scene);
                    current = owner == d.owner;
                    if (current && d.owner == m_validationCurrentOwner && !owner.starts_with("project://"))
                    {
                        std::string state;
                        if (!CaptureSceneState(state) || state != m_validationCurrentState) current = false;
                    }
                }
                if (!current && d.owner.starts_with("project://")) current = OpenSceneFromPath(m_project->ResolveAssetReference(d.owner));
                if (current && m_scene)
                {
                    if (auto *entity = m_scene->FindEntityByID(d.entity)) SetSelectedEntity(entity);
                    else m_statusMessage = "Validation owner no longer exists. Validate again.";
                }
                else m_statusMessage = "Open the owning scene or validate again to navigate.";
            }
            ImGui::EndDisabled();
            ImGui::Separator();
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::End();
    }
}
