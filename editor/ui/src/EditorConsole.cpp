// EditorShell's Console plumbing: logging, script exceptions, compiler diagnostics and navigation from entries.

#include "PlutoGE/ui/EditorShell.h"

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/ui/EditorSettings.h"
#include "PlutoGE/ui/ExternalScriptEditor.h"
#include "PlutoGE/ui/ScriptDiagnostics.h"

namespace PlutoGE::ui
{
    namespace
    {
        // Script errors are throttled before they reach the editor: occurrences jump 1, 10, 100, ... Each report
        // stands for the occurrences since the previous one, so merged Console rows show the true total.
        uint64_t OccurrencesSincePreviousReport(uint64_t occurrences)
        {
            return occurrences <= 1 ? 1 : occurrences - occurrences / 10;
        }
    }

    void EditorShell::Log(ConsoleSeverity severity, std::string message)
    {
        if (message.empty())
            return;
        m_consoleLog.Append(ConsoleEntry{.severity = severity, .text = std::move(message)});
    }

    void EditorShell::Log(ConsoleEntry entry)
    {
        m_consoleLog.Append(std::move(entry));
    }

    std::vector<EditorShell::ConsoleMessage> EditorShell::GetConsoleMessages() const
    {
        return m_consoleLog.Snapshot();
    }

    void EditorShell::ClearConsoleMessages()
    {
        m_consoleLog.Clear();
    }

    void EditorShell::OnScriptError(const scripting::ScriptErrorReport &report)
    {
        ConsoleEntry entry;
        entry.severity = ConsoleSeverity::Error;
        entry.source = ConsoleSource::Script;
        entry.entityId = report.entityId;
        entry.repeatCount = OccurrencesSincePreviousReport(report.occurrences);

        // Summary: "<Class>.<Callback> on '<Entity>': <exception type and message>"; the full trace goes in details.
        scripting::ScriptErrorReport summary = report;
        summary.occurrences = 1;
        summary.exception = std::string(FirstLineOf(report.exception));
        entry.text = scripting::FormatScriptErrorReport(summary);
        entry.details = report.exception;
        if (m_project)
        {
            if (auto location = FindExceptionSourceLocation(report.exception, m_project->GetRootDirectory()))
                entry.location = std::move(*location);
        }
        Log(std::move(entry));

        if (EditorSettings::Get().GetBool(kPauseOnScriptErrorKey, false) && m_engine.IsRuntimeRunning())
            m_scriptErrorPauseRequested = true;
    }

    void EditorShell::OnScriptLog(ConsoleSeverity severity, std::string_view message, uint32_t entityId, std::string_view file, int line)
    {
        ConsoleEntry entry;
        entry.severity = severity;
        entry.source = ConsoleSource::Script;
        entry.entityId = entityId;
        // Multi-line messages (exceptions with stack traces) show their first line; the rest is in the details.
        const auto firstLine = FirstLineOf(message);
        entry.text = std::string(firstLine);
        if (firstLine.size() != message.size())
            entry.details = std::string(message);
        if (!file.empty())
            entry.location = ConsoleFileLocation{.file = std::filesystem::path(std::string(file)), .line = line};
        else if (m_project)
        {
            if (auto location = FindExceptionSourceLocation(message, m_project->GetRootDirectory()))
                entry.location = std::move(*location);
        }
        Log(std::move(entry));
    }

    void EditorShell::ReportScriptBuildDiagnostics(std::string_view output, bool succeeded)
    {
        // A build supersedes the last one's messages, as fixed errors should disappear.
        m_consoleLog.RemoveSource(ConsoleSource::ScriptBuild);

        const auto root = m_project ? m_project->GetRootDirectory() : std::filesystem::path{};
        const auto diagnostics = ParseCompilerDiagnostics(output);
        std::size_t errors = 0;
        for (const auto &diagnostic : diagnostics)
        {
            errors += diagnostic.severity == ConsoleSeverity::Error ? 1 : 0;
            Log(ConsoleEntry{
                .severity = diagnostic.severity,
                .source = ConsoleSource::ScriptBuild,
                .text = FormatCompilerDiagnostic(diagnostic, root),
                .location = diagnostic.location,
            });
        }

        if (!succeeded && errors == 0)
        {
            // Restore failures, a missing SDK and similar problems have no source position; keep the raw output.
            Log(ConsoleEntry{
                .severity = ConsoleSeverity::Error,
                .source = ConsoleSource::ScriptBuild,
                .text = "Script build failed. Select this message for the build output.",
                .details = std::string(output),
            });
        }
    }

    void EditorShell::NavigateToConsoleEntry(const ConsoleEntry &entry)
    {
        if (entry.location.IsSet())
        {
            std::string error;
            if (!OpenInExternalScriptEditor(entry.location, &error))
                m_statusMessage = error;
            return;
        }
        if (entry.entityId != 0)
        {
            auto *entity = m_scene ? m_scene->FindEntityByID(entry.entityId) : nullptr;
            if (entity)
                SetSelectedEntity(entity);
            else
                m_statusMessage = "That entity no longer exists.";
            return;
        }
        if (!entry.asset.empty())
            RevealAsset(entry.asset);
    }
}
