#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/scripting/ScriptEngine.h"
#include "PlutoGE/scripting/ScriptBuildProcess.h"
#include "PlutoGE/ui/ExternalScriptEditor.h"
#include "PlutoGE/ui/ScriptDiagnostics.h"
#include <imgui.h>
#include <sstream>

namespace PlutoGE::ui
{
    void EditorShell::PollScriptSources()
    {
        if (m_scriptBuildFuture.valid() && !m_engine.IsRuntimeRunning() &&
            m_scriptBuildFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            try
            {
                auto result = m_scriptBuildFuture.get();
                if (m_project && m_project->GetManifestPath() == m_scriptBuildProject)
                {
                    m_scriptBuildOutput = std::move(result.output);
                    ReportScriptBuildDiagnostics(m_scriptBuildOutput, result.succeeded);
                    if (result.succeeded)
                    {
                        std::string error;
                        if (!ReloadProjectScriptAssembly(&error)) Log(ConsoleSeverity::Error, error);
                        else m_statusMessage = "Automatically built and reloaded scripts.";
                    }
                    else m_statusMessage = "Script build failed; previous assembly retained. See the Console.";
                }
            }
            catch (const std::exception &error) { Log(ConsoleSeverity::Error, error.what()); }
        }
        if (!m_autoBuildScripts || !m_project || IsRuntimeExportProject()) return;
        const auto now = ScriptSourceWatch::Clock::now();
        const auto root = m_project->GetManifestPath().parent_path();
        if (root != m_scriptWatchRoot)
        {
            // Join any previous project's read-only scan before replacing state.
            if (m_scriptWatchFuture.valid()) m_scriptWatchFuture.wait();
            m_scriptWatchFuture = {};
            m_scriptWatch.Reset();
            m_scriptWatchRoot = root;
            m_nextScriptScan = {};
        }
        if (m_scriptWatchFuture.valid() && m_scriptWatchFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            try
            {
                auto snapshot = m_scriptWatchFuture.get();
                // On the first scan of a project, rebuild an assembly left behind by an engine update, another
                // machine or edits made while the editor was closed: its calls into ScriptCore may no longer exist.
                const bool stale = !m_scriptWatch.IsPrimed() &&
                                   ScriptSourceWatch::IsAssemblyStale(ResolveProjectScriptAssemblyPath(),
                                                                      GetEditorScriptCoreAssemblyPath(), snapshot);
                m_scriptWatch.Observe(std::move(snapshot), now);
                if (stale)
                {
                    m_scriptWatch.Request(now);
                    Log(ConsoleSeverity::Info, "Project scripts are older than the engine or their sources; rebuilding.");
                }
            }
            catch (const std::exception &error) { Log(ConsoleSeverity::Warning, "Script source scan: " + std::string(error.what())); }
            m_nextScriptScan = now + std::chrono::seconds(1);
        }
        // Automatic compilation never interrupts a play session, bake or import.
        if (m_scriptWatch.Ready(now) && !m_scriptBuildFuture.valid() && !m_engine.IsRuntimeRunning() && !m_activeBakeTask && !IsModelImportRunning())
        {
            m_scriptWatch.Acknowledge();
            std::string error;
            if (!EnsureProjectScriptBuildScaffold(&error) || !SaveProjectManifest(&error)) Log(ConsoleSeverity::Error, error);
            else
            {
                scripting::ScriptBuildConfig config;
                config.projectPath = GetProjectScriptProjectPath();
                config.configuration = "Debug";
                config.framework = "net8.0";
                config.echoOutputToLog = false;
                m_scriptBuildProject = m_project->GetManifestPath();
                m_scriptBuildFuture = std::async(std::launch::async, [config] { scripting::ScriptEngine builder; return builder.BuildProject(config); });
                m_statusMessage = "Building scripts in the background...";
            }
        }
        if (!m_scriptWatchFuture.valid() && now >= m_nextScriptScan)
            m_scriptWatchFuture = std::async(std::launch::async, [root] { return ScriptSourceWatch::Capture(root); });
    }

    // Scene menu entries: gameplay kits to insert, and commands that edit the scene or selection.
    void EditorShell::RenderAuthoringMenuItems()
    {
        const bool canEditScene = m_project && m_scene && !m_engine.IsRuntimeRunning();
        const auto gameplayKits = m_authoring.List(AuthoringRegistry::Kind::GameplayKit);
        if (ImGui::BeginMenu("Insert Gameplay Kit", canEditScene && !gameplayKits.empty()))
        {
            for (const auto &entry : gameplayKits)
            {
                if (ImGui::MenuItem(entry.label.c_str(), nullptr, false, !entry.available || entry.available(*this)))
                {
                    try { ExecuteSceneEdit("Insert " + entry.label, [&] { entry.execute(*this); }); }
                    catch (const std::exception &error) { Log(ConsoleSeverity::Error, error.what()); }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", entry.description.c_str());
            }
            ImGui::EndMenu();
        }
        if (!ImGui::BeginMenu("Commands", canEditScene)) return;
        if (ImGui::BeginMenu("Project Commands", GetSelectedEntity() != nullptr))
        {
            auto &scripts = m_engine.GetScriptEngine();
            for (const auto &name : scripts.GetEditorCommandClassNames())
            {
                if (ImGui::MenuItem(name.c_str()))
                {
                    try
                    {
                        ExecuteSceneEdit(name, [&]
                        {
                            auto command = scripts.CreateInstance(name);
                            if (!command) throw std::runtime_error("Cannot create editor command: " + name);
                            command->SetOwner(GetSelectedEntity());
                            command->OnCreate();
                            command->OnDestroy();
                        });
                    }
                    catch (const std::exception &error) { Log(ConsoleSeverity::Error, error.what()); }
                }
            }
            ImGui::EndMenu();
        }
        for (const auto &entry : m_authoring.List(AuthoringRegistry::Kind::Command))
            if (ImGui::MenuItem(entry.label.c_str(), nullptr, false, !entry.available || entry.available(*this)))
            {
                try { ExecuteSceneEdit(entry.label, [&] { entry.execute(*this); }); }
                catch (const std::exception &error) { Log(ConsoleSeverity::Error, error.what()); }
            }
        ImGui::EndMenu();
    }

    void EditorShell::RenderExtensionInspectors()
    {
        for (const auto &entry : m_authoring.List(AuthoringRegistry::Kind::Inspector))
        {
            if (entry.available && !entry.available(*this)) continue;
            if (ImGui::CollapsingHeader(entry.label.c_str()))
            {
                try { entry.execute(*this); }
                catch (const std::exception &error) { ImGui::TextWrapped("%s", error.what()); }
            }
        }
    }

    void EditorShell::RenderScriptBuildDiagnostics()
    {
        if (!m_showScriptBuildDiagnostics) return;
        if (ImGui::Begin("Script Build Diagnostics", &m_showScriptBuildDiagnostics))
        {
            ImGui::TextUnformatted("Full output of the last script build. Errors and warnings also appear in the Console.");
            ImGui::TextDisabled("Select a diagnostic to open it in the external script editor (Preferences > External Tools).");
            std::istringstream lines(m_scriptBuildOutput);
            int index = 0;
            for (std::string line; std::getline(lines, line); ++index)
            {
                ImGui::PushID(index);
                const auto diagnostics = ParseCompilerDiagnostics(line);
                if (!diagnostics.empty())
                {
                    if (ImGui::Selectable(line.c_str()))
                    {
                        std::string error;
                        if (!OpenInExternalScriptEditor(diagnostics.front().location, &error)) Log(ConsoleSeverity::Error, error);
                    }
                }
                else ImGui::TextUnformatted(line.c_str());
                ImGui::PopID();
            }
        }
        ImGui::End();
    }
}
