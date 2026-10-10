#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/scripting/ScriptEngine.h"
#include "PlutoGE/scripting/ScriptBuildProcess.h"
#include <imgui.h>
#include <regex>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef APIENTRY
#undef APIENTRY
#endif
#include <Windows.h>
#include <shellapi.h>
#endif

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
                    if (result.succeeded)
                    {
                        std::string error;
                        if (!ReloadProjectScriptAssembly(&error)) Log(ConsoleSeverity::Error, error);
                        else m_statusMessage = "Automatically built and reloaded scripts.";
                    }
                    else { m_statusMessage = "Script build failed; previous assembly retained."; m_showScriptBuildDiagnostics = true; }
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
            try { m_scriptWatch.Observe(m_scriptWatchFuture.get(), now); }
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
                m_scriptBuildProject = m_project->GetManifestPath();
                m_scriptBuildFuture = std::async(std::launch::async, [config] { scripting::ScriptEngine builder; return builder.BuildProject(config); });
                m_statusMessage = "Building scripts in the background...";
            }
        }
        if (!m_scriptWatchFuture.valid() && now >= m_nextScriptScan)
            m_scriptWatchFuture = std::async(std::launch::async, [root] { return ScriptSourceWatch::Capture(root); });
    }

    void EditorShell::RenderAuthoringMenu()
    {
        if (!ImGui::BeginMenu("Authoring")) return;
        ImGui::MenuItem("Script Build Diagnostics", nullptr, &m_showScriptBuildDiagnostics);
        ImGui::BeginDisabled(!m_project || !m_scene || m_engine.IsRuntimeRunning());
        for (const auto &entry : m_authoring.List(AuthoringRegistry::Kind::GameplayKit))
        {
            if (ImGui::MenuItem(entry.label.c_str(), nullptr, false, !entry.available || entry.available(*this)))
            {
                try { ExecuteSceneEdit("Insert " + entry.label, [&] { entry.execute(*this); }); }
                catch (const std::exception &error) { Log(ConsoleSeverity::Error, error.what()); }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", entry.description.c_str());
        }
        ImGui::Separator();
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
        ImGui::EndDisabled();
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
            ImGui::TextUnformatted("Select a source diagnostic to open its file. The compiler line/column is shown below.");
            std::istringstream lines(m_scriptBuildOutput);
            static const std::regex diagnostic(R"(^(.+)\(([0-9]+),([0-9]+)\): (error|warning) (.+)$)");
            int index = 0;
            for (std::string line; std::getline(lines, line); ++index)
            {
                std::smatch match;
                ImGui::PushID(index);
                if (std::regex_match(line, match, diagnostic))
                {
                    if (ImGui::Selectable(line.c_str()))
                    {
                        const auto path = std::filesystem::path(match[1].str());
#ifdef _WIN32
                        const auto result = reinterpret_cast<std::intptr_t>(ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
                        if (result <= 32) Log(ConsoleSeverity::Error, "Cannot open diagnostic source: " + path.string());
#else
                        scripting::RunBuildProcess({"xdg-open", path.string()});
#endif
                    }
                }
                else ImGui::TextUnformatted(line.c_str());
                ImGui::PopID();
            }
        }
        ImGui::End();
    }
}
