// Headless tests for the editor workflow building blocks: Console log model, per-user settings, compiler and
// exception parsing, external editor commands, and script error throttling.

#include "PlutoGE/scripting/ScriptErrors.h"
#include "PlutoGE/scripting/ScriptLogging.h"
#include "PlutoGE/ui/ConsoleLog.h"
#include "PlutoGE/ui/EditorSettings.h"
#include "PlutoGE/ui/ExternalScriptEditor.h"
#include "PlutoGE/ui/ScriptDiagnostics.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace PlutoGE;
    using namespace PlutoGE::ui;

    void Check(bool value, const char *message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    void ConsoleLogMergesAndBounds()
    {
        ConsoleLog log(3);
        log.Append({.severity = ConsoleSeverity::Info, .text = "a"});
        log.Append({.severity = ConsoleSeverity::Info, .text = "a"});
        log.Append({.severity = ConsoleSeverity::Warning, .text = "a"});
        auto entries = log.Snapshot();
        Check(entries.size() == 2 && entries[0].repeatCount == 2, "Consecutive identical messages merge");
        Check(entries[0].sequence != entries[1].sequence, "Entries get distinct sequence numbers");

        log.Append({.severity = ConsoleSeverity::Info, .text = "b"});
        log.Append({.severity = ConsoleSeverity::Info, .text = "c"});
        entries = log.Snapshot();
        Check(entries.size() == 3 && entries.front().severity == ConsoleSeverity::Warning, "Capacity drops the oldest");

        const auto counts = log.CountBySeverity();
        Check(counts[0] == 2 && counts[1] == 1 && counts[2] == 0, "Severity counts");

        uint64_t revision = 0;
        std::vector<ConsoleEntry> copy;
        Check(log.CopyIfChanged(revision, copy) && copy.size() == 3, "First copy");
        Check(!log.CopyIfChanged(revision, copy), "Unchanged log is not copied again");

        ConsoleLog builds;
        builds.Append({.text = "editor"});
        builds.Append({.severity = ConsoleSeverity::Error, .source = ConsoleSource::ScriptBuild, .text = "CS1002"});
        builds.Append({.severity = ConsoleSeverity::Warning, .source = ConsoleSource::ScriptBuild, .text = "CS0168"});
        builds.RemoveSource(ConsoleSource::ScriptBuild);
        Check(builds.Snapshot().size() == 1 && builds.Snapshot()[0].text == "editor", "RemoveSource drops only that source");

        const auto before = log.GetRevision();
        log.Append({.text = "", .entityId = 1});
        Check(log.GetRevision() == before, "Empty message ignored");
        log.Clear();
        Check(log.Snapshot().empty(), "Clear");
    }

    void CollapseMergesAcrossPositions()
    {
        std::vector<ConsoleEntry> entries{
            {.text = "x", .repeatCount = 2},
            {.text = "y"},
            {.text = "x"},
            {.text = "x", .entityId = 4}, // Different context stays separate.
        };
        const auto collapsed = CollapseConsoleEntries(entries);
        Check(collapsed.size() == 3, "Collapse count");
        Check(collapsed[0].text == "x" && collapsed[0].repeatCount == 3, "Collapse adds repeats");
        Check(collapsed[1].text == "y" && collapsed[2].entityId == 4, "Collapse keeps first-occurrence order");
    }

    void SettingsRoundTrip()
    {
        const auto file = std::filesystem::temp_directory_path() / "PlutoGEEditorWorkflowTests" / "settings.ini";
        std::filesystem::remove_all(file.parent_path());
        {
            EditorSettings settings(file);
            Check(!settings.Has("a") && settings.GetBool("a", true), "Fallback for missing key");
            settings.SetBool("console.pauseOnError", true);
            settings.SetInt("count", 42);
            settings.SetFloat("speed", 2.5f);
            settings.SetString("command", "code --goto \"{file}:{line}\"\nsecond line\\");
            Check(settings.IsDirty() && settings.Flush() && !settings.IsDirty(), "Flush writes");
        }
        EditorSettings reloaded(file);
        Check(reloaded.GetBool("console.pauseOnError", false), "Bool round trip");
        Check(reloaded.GetInt("count", 0) == 42, "Int round trip");
        Check(reloaded.GetFloat("speed", 0.0f) == 2.5f, "Float round trip");
        Check(reloaded.GetString("command") == "code --goto \"{file}:{line}\"\nsecond line\\", "String escaping round trip");
        Check(reloaded.GetInt("command", 7) == 7, "Unparsable value falls back");
        reloaded.SetInt("count", 42);
        Check(!reloaded.IsDirty(), "Setting the same value is not a change");
        reloaded.Remove("count");
        Check(reloaded.IsDirty() && !reloaded.Has("count"), "Remove");
        std::filesystem::remove_all(file.parent_path());
    }

    void CompilerDiagnosticsParse()
    {
        const std::string output =
            "  Determining projects to restore...\n"
            "C:\\Game\\Assets\\Scripts\\Player.cs(12,5): error CS0103: The name 'x' does not exist in the current context [C:\\Game\\Game.csproj]\r\n"
            "C:\\Game\\Assets\\Scripts\\Enemy.cs(3,1): warning CS0168: The variable 'e' is declared but never used [C:\\Game\\Game.csproj]\n"
            "Build FAILED.\n"
            "C:\\Game\\Assets\\Scripts\\Player.cs(12,5): error CS0103: The name 'x' does not exist in the current context [C:\\Game\\Game.csproj]\n"
            "    1 Error(s)\n";
        const auto diagnostics = ParseCompilerDiagnostics(output);
        Check(diagnostics.size() == 2, "Summary duplicates dropped");
        Check(diagnostics[0].severity == ConsoleSeverity::Error && diagnostics[0].code == "CS0103", "Error parsed");
        Check(diagnostics[0].location.line == 12 && diagnostics[0].location.column == 5, "Line and column parsed");
        Check(diagnostics[0].message == "The name 'x' does not exist in the current context", "Project suffix trimmed");
        Check(diagnostics[1].severity == ConsoleSeverity::Warning, "Warning parsed");
        const auto text = FormatCompilerDiagnostic(diagnostics[0], "C:\\Game");
        Check(text == "Assets/Scripts/Player.cs(12,5): CS0103 The name 'x' does not exist in the current context", "Relative format");
        Check(ParseCompilerDiagnostics("Time Elapsed 00:00:01.23 (3): note").empty(), "Non-diagnostic lines ignored");
    }

    void ExceptionLocations()
    {
        const std::string exception =
            "System.NullReferenceException: Object reference not set to an instance of an object.\n"
            "   at PlutoGE.ScriptCore.GameObject.GetComponent[T]() in C:\\dev\\PlutoGE\\engine\\ScriptCore\\GameObject.cs:line 80\n"
            "   at Game.Player.OnUpdate(Single deltaTime) in C:\\Game\\Assets\\Scripts\\Player.cs:line 42\n"
            "   at PlutoGE.ScriptCore.Native.ScriptBridge.InvokeOnUpdate(Int64 handle, Single deltaTime)\n";
        auto location = FindExceptionSourceLocation(exception, "C:\\Game");
        Check(location && location->line == 42 && location->file.filename() == "Player.cs", "Project frame preferred");
        location = FindExceptionSourceLocation(exception);
        Check(location && location->line == 80, "First frame without a root");
        location = FindExceptionSourceLocation(exception, "D:\\Elsewhere");
        Check(location && location->line == 80, "Falls back to first frame outside the root");
        Check(!FindExceptionSourceLocation("System.Exception: no trace"), "No frame, no location");
        Check(FirstLineOf("one\r\ntwo") == "one", "First line");
    }

    void ExternalEditorCommands()
    {
        ConsoleFileLocation location{.file = "C:/Game/My Scripts/Player.cs", .line = 42, .column = 7};
        auto arguments = ExpandExternalEditorCommand("code --goto \"{file}:{line}:{column}\"", location);
        Check(arguments.size() == 3 && arguments[0] == "code" && arguments[1] == "--goto", "Command split");
        Check(arguments[2].find("Player.cs:42:7") != std::string::npos && arguments[2].find("My Scripts") != std::string::npos,
              "Placeholders substituted inside a quoted argument");
        arguments = ExpandExternalEditorCommand("rider   --line {line}  \"{file}\"", ConsoleFileLocation{.file = "a.cs"});
        Check(arguments.size() == 4 && arguments[2] == "1", "Missing line becomes 1; repeated spaces ignored");
        Check(ExpandExternalEditorCommand("", location).empty(), "Empty command means system default");
        Check(ExpandExternalEditorCommand("tool \"\"", location).size() == 2, "Explicit empty argument kept");

        EditorSettings settings(std::filesystem::temp_directory_path() / "PlutoGEEditorWorkflowTests-editor.ini");
        settings.SetString(kExternalEditorPresetKey, "vscode");
        Check(GetExternalScriptEditorCommand(settings).starts_with("code "), "Preset command");
        settings.SetString(kExternalEditorPresetKey, "custom");
        settings.SetString(kExternalEditorCommandKey, "myeditor {file}");
        Check(GetExternalScriptEditorCommand(settings) == "myeditor {file}", "Custom command");
        settings.SetString(kExternalEditorPresetKey, "unknown");
        Check(GetExternalScriptEditorCommand(settings).empty(), "Unknown preset falls back to system default");
    }

    void ScriptErrorThrottling()
    {
        scripting::ScriptErrorThrottle throttle;
        std::vector<uint64_t> reported;
        for (int i = 0; i < 1000; ++i)
            if (const auto count = throttle.Record("OnUpdate", "System.Exception: boom\n   at A.B()"))
                reported.push_back(count);
        Check((reported == std::vector<uint64_t>{1, 10, 100, 1000}), "Repeats reported at powers of ten");
        Check(throttle.Record("OnUpdate", "System.Exception: other") == 1, "A different error is reported at once");
        Check(throttle.Record("OnFixedUpdate", "System.Exception: boom\n   at C.D()") == 1, "Keyed by callback");

        scripting::ScriptErrorReport report{.scriptClass = "Player", .callback = "OnUpdate", .entityId = 4,
                                            .entityName = "Hero", .exception = "System.Exception: boom", .occurrences = 10};
        Check(scripting::FormatScriptErrorReport(report) == "Player.OnUpdate on 'Hero' (entity 4) threw (10 times): System.Exception: boom",
              "Report format");

        // Without a listener, reports go to the script log as errors.
        std::string logged;
        scripting::SetScriptLogSink([&](scripting::ScriptLogSeverity severity, std::string_view message)
                                    { if (severity == scripting::ScriptLogSeverity::Error) logged = message; });
        scripting::DispatchScriptError(report);
        Check(logged.find("Player.OnUpdate") != std::string::npos, "Fallback to script log");
        int heard = 0;
        scripting::SetScriptErrorListener([&](const scripting::ScriptErrorReport &) { ++heard; });
        logged.clear();
        scripting::DispatchScriptError(report);
        Check(heard == 1 && logged.empty(), "Listener replaces the log fallback");
        scripting::ClearScriptErrorListener();
        scripting::ClearScriptLogSink();
    }
}

int main()
{
    try
    {
        ConsoleLogMergesAndBounds();
        CollapseMergesAcrossPositions();
        SettingsRoundTrip();
        CompilerDiagnosticsParse();
        ExceptionLocations();
        ExternalEditorCommands();
        ScriptErrorThrottling();
    }
    catch (const std::exception &error)
    {
        std::cerr << "EditorWorkflowTests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "EditorWorkflowTests passed\n";
    return 0;
}
