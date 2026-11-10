// Headless tests for the editor workflow building blocks: Console log model, per-user settings, compiler and
// exception parsing, external editor commands, and script error throttling.

#include "PlutoGE/scripting/ScriptErrors.h"
#include "PlutoGE/scripting/ScriptLogging.h"
#include "PlutoGE/ui/AssetHotReload.h"
#include "PlutoGE/ui/ConsoleLog.h"
#include "PlutoGE/ui/EditorCameraNavigation.h"
#include "PlutoGE/ui/EditorCommands.h"
#include "PlutoGE/ui/EditorDocument.h"
#include "PlutoGE/ui/EditorSettings.h"
#include "PlutoGE/ui/EntityNaming.h"
#include "PlutoGE/ui/ExternalScriptEditor.h"
#include "PlutoGE/ui/ProjectScaffold.h"
#include "PlutoGE/ui/SceneRecovery.h"
#include "PlutoGE/ui/ScriptDiagnostics.h"
#include "PlutoGE/ui/UnsavedSwitchGuard.h"
#include "PlutoGE/ui/ViewportToolPreferences.h"

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <iostream>
#include <iterator>
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

        const ExternalEditorProjectContext project{.projectRoot = "C:/Game", .solution = "C:/Game/Game.sln"};
        arguments = ExpandExternalEditorCommand("code \"{projectRoot}\" --goto \"{file}:{line}\"", location, project);
        Check(arguments.size() == 4 && std::filesystem::path(arguments[1]) == std::filesystem::path("C:/Game").make_preferred(),
              "Project root substituted");
        arguments = ExpandExternalEditorCommand("rider \"{solution}\" \"{file}\"", location, project);
        Check(arguments.size() == 3 && arguments[1].ends_with("Game.sln"), "Solution substituted");
        arguments = ExpandExternalEditorCommand("code \"{projectRoot}\" --goto \"{file}:{line}\"", location);
        Check(arguments.size() == 3 && arguments[1] == "--goto", "Placeholder-only argument dropped without a project");

        EditorSettings settings(std::filesystem::temp_directory_path() / "PlutoGEEditorWorkflowTests-editor.ini");
        settings.SetString(kExternalEditorPresetKey, "vscode");
        Check(GetExternalScriptEditorCommand(settings).starts_with("code "), "Preset command");
        settings.SetString(kExternalEditorPresetKey, "custom");
        settings.SetString(kExternalEditorCommandKey, "myeditor {file}");
        Check(GetExternalScriptEditorCommand(settings) == "myeditor {file}", "Custom command");
        settings.SetString(kExternalEditorPresetKey, "unknown");
        Check(GetExternalScriptEditorCommand(settings).empty(), "Unknown preset falls back to system default");
    }

    struct FakeDocument final : IEditorDocument
    {
        FakeDocument(std::string documentName, bool isDirty) : name(std::move(documentName)), dirty(isDirty) {}

        std::string name;
        bool dirty = false;
        bool failSave = false;
        int saves = 0, discards = 0, focuses = 0;

        std::string GetDocumentName() const override { return name; }
        bool HasUnsavedChanges() const override { return dirty; }
        bool SaveDocument(std::string &error) override
        {
            ++saves;
            if (failSave)
            {
                error = "disk full";
                return false;
            }
            dirty = false;
            return true;
        }
        void DiscardChanges() override { ++discards; dirty = false; }
        void FocusDocument() override { ++focuses; }
    };

    void UnsavedDocumentsResolve()
    {
        FakeDocument scene("Scene Main", true);
        FakeDocument clean("Material Clean", false);
        FakeDocument material("Material Rock", true);
        std::vector<IEditorDocument *> documents{&scene, &clean, &material};
        std::string error;

        std::vector<std::string> asked;
        const auto answer = [&](UnsavedChangesChoice choice)
        {
            return [&asked, choice](std::span<const std::string> names)
            {
                asked.assign(names.begin(), names.end());
                return choice;
            };
        };

        Check(!ResolveUnsavedDocuments(documents, answer(UnsavedChangesChoice::Cancel), error), "Cancel stops");
        Check((asked == std::vector<std::string>{"Scene Main", "Material Rock"}), "Only dirty documents are listed");
        Check(scene.dirty && material.dirty && scene.saves == 0, "Cancel changes nothing");

        material.failSave = true;
        Check(!ResolveUnsavedDocuments(documents, answer(UnsavedChangesChoice::Save), error), "A failed save stops");
        Check(!scene.dirty && material.dirty && material.focuses == 1, "Failed document keeps edits and is focused");
        Check(error.find("Material Rock") != std::string::npos && error.find("disk full") != std::string::npos, "Save failure reason");

        material.failSave = false;
        Check(ResolveUnsavedDocuments(documents, answer(UnsavedChangesChoice::Save), error) && !material.dirty, "Save all");

        material.dirty = true;
        Check(ResolveUnsavedDocuments(documents, answer(UnsavedChangesChoice::Discard), error) && material.discards == 1 &&
                  clean.discards == 0, "Discard only touches dirty documents");

        bool askedAgain = false;
        Check(ResolveUnsavedDocuments(documents, [&](std::span<const std::string>) { askedAgain = true; return UnsavedChangesChoice::Cancel; }, error) &&
                  !askedAgain, "Nothing dirty: no question");

        const std::array<std::string, 2> names{"Scene Main", "Material Rock"};
        const auto message = FormatUnsavedChangesMessage(names);
        Check(message.find("these") != std::string::npos && message.find("Material Rock") != std::string::npos, "Prompt lists documents");
    }

    void SwitchGuardFollowsSelection()
    {
        UnsavedSwitchGuard guard;
        Check(guard.Follow("a", "", false) == std::optional<std::string>("a"), "First selection loads");
        Check(!guard.Follow("a", "a", false), "Same selection does nothing");
        Check(guard.Follow("b", "a", false) == std::optional<std::string>("b"), "Clean editor follows at once");
        Check(!guard.Follow("c", "b", true) && guard.IsPromptPending(), "Dirty editor stays and asks");
        Check(!guard.Follow("c", "b", true), "No repeated prompt for the same selection");
        Check(!guard.Follow("", "b", false), "Clearing the selection keeps the shown asset");
        guard.Reset();
        Check(!guard.IsPromptPending() && guard.Follow("b", "", false) == std::optional<std::string>("b"), "Reset re-follows");
    }

    void DuplicateNames()
    {
        const std::vector<std::string> none;
        Check(NextDuplicateName("Crate", none) == "Crate (1)", "First duplicate");
        const std::vector<std::string> siblings{"Crate", "Crate (1)", "Crate (3)"};
        Check(NextDuplicateName("Crate", siblings) == "Crate (2)", "Fills the first free number");
        Check(NextDuplicateName("Crate (1)", siblings) == "Crate (2)", "Numbered names keep their base");
        Check(NextDuplicateName("Crate Copy Copy", siblings) == "Crate (2)", "Legacy Copy suffixes fold");
        Check(NextDuplicateName("Level (Night)", none) == "Level (Night) (1)", "Non-numeric brackets are part of the name");
        Check(NextDuplicateName("Wall (12)", none) == "Wall (1)", "Number replaced");
    }

    void RecoverySessionMarker()
    {
        const auto directory = std::filesystem::temp_directory_path() / "PlutoGEEditorWorkflowTests" / "recovery";
        std::filesystem::remove_all(directory);
        Check(!SceneRecovery::BeginSession(directory), "First session is clean");
        Check(SceneRecovery::BeginSession(directory), "Session left open reads as an unclean exit");
        SceneRecovery::EndSession(directory);
        Check(!SceneRecovery::BeginSession(directory), "Ended session is clean");
        SceneRecovery::EndSession(directory);
        std::vector<std::string> errors;
        Check(SceneRecovery::List(directory, errors).empty() && errors.empty(), "Marker is not listed as a backup");
        std::filesystem::remove_all(directory.parent_path());
    }

    void ProjectScaffolding()
    {
        const ScriptProjectLayout layout{.rootNamespace = "Demo", .sourcePattern = "Assets/**/*.cs", .managedOutputPath = "Assets/Managed",
                                         .scriptCoreReference = "C:/Engine/PlutoGE.ScriptCore.dll", .scriptCoreRuntimeDirectory = "C:/Engine"};
        const auto project = BuildScriptProjectText(layout);
        Check(project.find("C:/Engine") == std::string::npos, "Script project contains a machine path");
        Check(project.find("$(MSBuildProjectName).engine.props") != std::string::npos &&
                  project.find("$(MSBuildProjectName).custom.props") != std::string::npos &&
                  project.find("$(MSBuildProjectName).custom.targets") != std::string::npos,
              "Script project imports engine and custom files");
        const auto props = BuildScriptEnginePropsText(layout);
        Check(props.find("<PlutoGEScriptCore>C:/Engine/PlutoGE.ScriptCore.dll</PlutoGEScriptCore>") != std::string::npos &&
                  props.find("<PlutoGEScriptCoreIsProject>false</PlutoGEScriptCoreIsProject>") != std::string::npos,
              "Engine props carry this machine's paths");
        Check(GetScriptEnginePropsPath("C:/Game/Demo.Scripts.csproj").filename() == "Demo.Scripts.engine.props", "Engine props path");

        const auto root = std::filesystem::temp_directory_path() / "PlutoGEEditorWorkflowTests" / "scaffold";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        { std::ofstream(root / ".gitignore") << "mine\n"; }
        std::string error;
        Check(WriteNewProjectFiles(root, &error), error.c_str());
        std::ifstream ignore(root / ".gitignore");
        std::string firstLine;
        std::getline(ignore, firstLine);
        ignore.close();
        Check(firstLine == "mine", "An existing .gitignore was overwritten");
        Check(std::filesystem::exists(root / ".gitattributes") && std::filesystem::exists(root / ".vscode" / "launch.json"),
              "Version control and debugger files written");
        Check(BuildGitAttributesText().find("*.plutometa text -filter") != std::string::npos, "plutometa kept out of LFS");
        Check(BuildGitIgnoreText().find("*.engine.props") != std::string::npos, "engine props ignored");
        Check(BuildGitIgnoreText().find("/*.sln") != std::string::npos, "per-machine solution ignored");
        std::filesystem::remove_all(root.parent_path());
    }

    void ScriptSolutionGeneration()
    {
        Check(GetScriptSolutionPath("C:/Game/Demo.Scripts.csproj") == std::filesystem::path("C:/Game/Demo.sln"), "Solution path");

        const std::vector<SolutionProject> scriptsOnly{{"Demo.Scripts", "Demo.Scripts.csproj"}};
        const std::vector<SolutionProject> withEngine{{"Demo.Scripts", "Demo.Scripts.csproj"},
                                                      {"PlutoGE.ScriptCore", "../Engine/ScriptCore/PlutoGE.ScriptCore.csproj"}};
        const auto text = BuildScriptSolutionText(withEngine);
        Check(text == BuildScriptSolutionText(withEngine), "Solution text is deterministic");
        Check(text.find("Microsoft Visual Studio Solution File, Format Version 12.00") != std::string::npos, "Solution header");
        Check(text.find("..\\Engine\\ScriptCore\\PlutoGE.ScriptCore.csproj") != std::string::npos, "Solution uses Windows separators");
        Check(text.find(".Debug|Any CPU.Build.0 = Debug|Any CPU") != std::string::npos, "Projects build in Debug");
        Check(ReadSolutionProjects(text) == withEngine, "Solution projects round-trip");
        Check(ReadSolutionProjects(BuildScriptSolutionText(scriptsOnly)) == scriptsOnly, "Single-project solution");

        const auto root = std::filesystem::temp_directory_path() / "PlutoGEEditorWorkflowTests" / "solution";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        const auto solution = root / "Demo.sln";
        const std::array<std::string, 2> managed{"Demo.Scripts.csproj", "PlutoGE.ScriptCore.csproj"};
        const auto readSolution = [&]
        {
            std::ifstream input(solution, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(input)), {});
        };
        std::string error;

        Check(UpdateScriptSolution(solution, withEngine, managed, &error) && readSolution() == text, "Missing solution written");

        // An IDE may rewrite the file (e.g. add a SolutionGuid section); the same projects mean nothing to do.
        { std::ofstream(solution, std::ios::app | std::ios::binary) << "# formatting an IDE added\r\n"; }
        const auto touched = readSolution();
        Check(UpdateScriptSolution(solution, withEngine, managed, &error) && readSolution() == touched,
              "Up-to-date solution left untouched");

        Check(UpdateScriptSolution(solution, scriptsOnly, managed, &error) &&
                  ReadSolutionProjects(readSolution()) == scriptsOnly,
              "Editor-owned solution refreshed when the engine reference changes");

        std::vector<SolutionProject> userProjects = scriptsOnly;
        userProjects.push_back({"Demo.Tests", "Tests/Demo.Tests.csproj"});
        { std::ofstream(solution, std::ios::trunc | std::ios::binary) << BuildScriptSolutionText(userProjects); }
        Check(UpdateScriptSolution(solution, withEngine, managed, &error) &&
                  ReadSolutionProjects(readSolution()) == userProjects,
              "Solution with user projects is left alone");

        std::filesystem::remove_all(root.parent_path());
    }

    void AssetHotReloadWatch()
    {
        Check(ClassifyHotReloadPath("Textures/Rock.PNG") == HotReloadKind::Texture, "Texture classified");
        Check(ClassifyHotReloadPath("Materials/Rock.plutomaterial") == HotReloadKind::Material, "Material classified");
        Check(ClassifyHotReloadPath("Audio/Hit.wav") == HotReloadKind::Audio, "Audio classified");
        Check(!ClassifyHotReloadPath("Scripts/Player.cs"), "Scripts are not hot reloaded here");

        using Clock = AssetFileWatch::Clock;
        const auto t0 = Clock::time_point{} + std::chrono::seconds(10);
        const auto stamp = [](std::uintmax_t size) { return AssetFileWatch::Stamp{std::filesystem::file_time_type{}, size}; };
        AssetFileWatch watch;
        Check(watch.Observe({{"a.png", stamp(1)}}, t0).empty(), "Baseline reports nothing");
        Check(watch.Observe({{"a.png", stamp(2)}}, t0 + std::chrono::milliseconds(100)).empty(), "A change waits for the quiet period");
        Check(watch.Observe({{"a.png", stamp(3)}}, t0 + std::chrono::milliseconds(300)).empty(), "A file still being written waits");
        Check(watch.Observe({{"a.png", stamp(3)}}, t0 + std::chrono::milliseconds(500)).empty(), "Quiet period restarts on each change");
        const auto changed = watch.Observe({{"a.png", stamp(3)}, }, t0 + std::chrono::milliseconds(800));
        Check(changed.size() == 1 && changed[0] == "a.png", "Stable change reported once");
        Check(watch.Observe({{"a.png", stamp(3)}}, t0 + std::chrono::seconds(2)).empty(), "No repeat report");
        Check(watch.Observe({}, t0 + std::chrono::seconds(3)).empty() && watch.Observe({}, t0 + std::chrono::seconds(4)).empty(),
              "Deletions are not reported");
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
    void EditorCommandsRebindAndRun()
    {
        const auto file = std::filesystem::temp_directory_path() / "PlutoGEEditorWorkflowTests" / "commands.ini";
        std::filesystem::remove_all(file.parent_path());
        EditorSettings settings(file);
        EditorCommandRegistry commands(&settings);
        commands.Register({.id = "scene.duplicate", .label = "Duplicate Entity", .category = "Edit", .defaultShortcut = ImGuiMod_Ctrl | ImGuiKey_D});
        commands.Register({.id = "file.save", .label = "Save", .category = "File", .defaultShortcut = ImGuiMod_Ctrl | ImGuiKey_S});
        commands.Register({.id = "file.save", .label = "Duplicate id", .category = "File"});
        Check(commands.List().size() == 2, "Duplicate ids are ignored");

        Check(commands.GetShortcut("scene.duplicate") == (ImGuiMod_Ctrl | ImGuiKey_D) && !commands.IsCustomized("scene.duplicate"),
              "Default shortcut without a user binding");
        commands.SetShortcut("scene.duplicate", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_D);
        Check(commands.IsCustomized("scene.duplicate") && settings.Has("keys.scene.duplicate"), "Rebinding is stored in user settings");
        Check(EditorCommandRegistry(&settings).GetShortcut("scene.duplicate") == 0 , "Unregistered commands have no shortcut");
        commands.SetShortcut("file.save", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_D);
        Check(commands.FindByShortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_D, "file.save")->id == "scene.duplicate", "Conflicts are found");
        commands.SetShortcut("file.save", ImGuiKey_None);
        Check(commands.GetShortcut("file.save") == ImGuiKey_None && commands.IsCustomized("file.save"), "A shortcut can be removed");
        commands.SetShortcut("file.save", ImGuiMod_Ctrl | ImGuiKey_S);
        Check(!commands.IsCustomized("file.save"), "Choosing the default again clears the override");
        commands.ResetShortcut("scene.duplicate");
        Check(commands.GetShortcut("scene.duplicate") == (ImGuiMod_Ctrl | ImGuiKey_D), "Reset restores the default");

        int ran = 0;
        commands.BeginFrame();
        commands.Bind("scene.duplicate", [&] { ++ran; });
        commands.Bind("file.save", [&] { ran += 10; }, false);
        Check(commands.IsAvailable("scene.duplicate") && !commands.IsAvailable("file.save"), "Disabled bindings are unavailable");
        Check(commands.Run("scene.duplicate") && !commands.Run("file.save") && ran == 1, "Run calls only enabled handlers");
        commands.Queue("scene.duplicate");
        commands.BeginFrame();
        commands.RunQueued();
        Check(ran == 1, "Unbound commands do not run");
        commands.Bind("scene.duplicate", [&] { ++ran; });
        commands.Queue("scene.duplicate");
        commands.RunQueued();
        commands.RunQueued();
        Check(ran == 2, "A queued command runs once with the current binding");

        Check(FormatShortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S) == "Ctrl+Shift+S", "Chord text");
        Check(FormatShortcut(ImGuiMod_Ctrl | ImGuiKey_Comma) == "Ctrl+," && FormatShortcut(ImGuiKey_F5) == "F5", "Punctuation and function keys");
        Check(FormatShortcut(ImGuiMod_Alt | ImGuiKey_1) == "Alt+1" && FormatShortcut(ImGuiKey_None).empty(), "Digits and unbound");

        Check(FuzzyMatchScore("Edit: Duplicate Entity", "dup") > FuzzyMatchScore("Edit: Drop Selection onto Ground...", "dup"),
              "Consecutive word-start matches rank first");
        Check(FuzzyMatchScore("File: Save Scene As...", "ssa") >= 0 && FuzzyMatchScore("File: Save", "xyz") < 0, "Subsequence matching");
        Check(FuzzyMatchScore("Player.plutoprefab", "PLAYER") > FuzzyMatchScore("Players/Enemy.plutoprefab", "player"), "Case-insensitive, shorter first");
        Check(FuzzyMatchScore("anything", "") == 0, "Empty query matches everything");
    }
    bool Near(const glm::vec3 &a, const glm::vec3 &b, float tolerance = 1e-3f)
    {
        return glm::length(a - b) <= tolerance;
    }

    void ViewportNavigation()
    {
        // Looking down -Z from 10 m in front of the origin.
        const EditorCameraPose pose{{0.0f, 0.0f, 10.0f}, 0.0f, 0.0f};
        const glm::vec3 pivot(0.0f);
        Check(Near(GetEditorCameraForward(0.0f, 0.0f), {0.0f, 0.0f, -1.0f}), "Zero yaw and pitch look down -Z");

        const auto quarter = OrbitEditorCamera(pose, pivot, 90.0f, 0.0f, 89.0f);
        Check(Near(quarter.position, {10.0f, 0.0f, 0.0f}), "Orbiting 90 degrees of yaw swings to the side");
        Check(Near(GetEditorCameraForward(quarter.yawDegrees, quarter.pitchDegrees), {-1.0f, 0.0f, 0.0f}), "Still facing the pivot");
        const auto raised = OrbitEditorCamera(pose, pivot, 0.0f, -30.0f, 89.0f);
        Check(std::abs(glm::length(raised.position) - 10.0f) < 1e-3f && raised.position.y > 4.9f, "Orbit keeps the distance and rises");
        const auto clamped = OrbitEditorCamera(pose, pivot, 0.0f, -200.0f, 89.0f);
        Check(clamped.pitchDegrees == -89.0f && clamped.position.y < 10.0f, "Pitch stops at the limit");
        // An off-centre pivot keeps its place relative to the camera.
        const EditorCameraPose turned{{0.0f, 0.0f, 10.0f}, 20.0f, 0.0f};
        const auto offCentre = OrbitEditorCamera(turned, pivot, 45.0f, 0.0f, 89.0f);
        Check(std::abs(glm::length(offCentre.position) - 10.0f) < 1e-3f && offCentre.yawDegrees == 65.0f, "Rigid orbit around any pivot");

        Check(IsUsableOrbitPivot(pose, pivot, 50.0f, 1000.0f), "A pivot ahead is usable");
        Check(!IsUsableOrbitPivot(pose, {0.0f, 0.0f, 20.0f}, 50.0f, 1000.0f), "A pivot behind is not");
        Check(!IsUsableOrbitPivot(pose, {20.0f, 0.0f, 9.0f}, 50.0f, 1000.0f), "A pivot far to the side is not");

        const auto closer = DollyEditorCamera(pose, pivot, 1.0f);
        Check(Near(closer.position, {0.0f, 0.0f, 8.5f}) && Near(closer.pivot, pivot), "One notch covers 15% of the distance");
        const auto back = DollyEditorCamera(pose, pivot, -1.0f);
        Check(back.position.z > 10.0f && Near(back.pivot, pivot), "Wheel down backs away");
        const auto through = DollyEditorCamera({{0.0f, 0.0f, 0.4f}, 0.0f, 0.0f}, pivot, 3.0f);
        Check(through.position.z < 0.4f && through.pivot.z < through.position.z, "Past the pivot it moves along instead of stopping");
        const auto behind = DollyEditorCamera(pose, {0.0f, 0.0f, 30.0f}, 1.0f);
        Check(behind.position.z < 10.0f && behind.pivot.z < 10.0f, "A pivot behind is replaced by one ahead");
        const auto escape = DollyEditorCamera({{0.0f, 0.0f, 0.1f}, 0.0f, 0.0f}, pivot, -1.0f);
        const auto escape2 = DollyEditorCamera({escape.position, 0.0f, 0.0f}, escape.pivot, -1.0f);
        Check(escape2.position.z - escape.position.z > escape.position.z - 0.1f, "Backing away from very close speeds up");
    }

    void ViewportToolSettingsPersist()
    {
        const auto file = std::filesystem::temp_directory_path() / "PlutoGEEditorWorkflowTests" / "viewport.ini";
        std::filesystem::remove_all(file.parent_path());
        EditorSettings settings(file);
        Check(LoadViewportToolPreferences(settings) == ViewportToolPreferences{}, "Defaults without saved values");
        ViewportToolPreferences chosen;
        chosen.tool = ViewportToolPreferences::Tool::Rotate;
        chosen.localSpace = false;
        chosen.snap = true;
        chosen.moveSnap = {0.25f, 0.5f, 2.0f};
        chosen.rotateSnapDegrees = 45.0f;
        chosen.scaleSnap = 0.5f;
        chosen.showGrid = false;
        SaveViewportToolPreferences(settings, chosen);
        Check(settings.Flush(), "Viewport settings flush");
        Check(LoadViewportToolPreferences(EditorSettings(file)) == chosen, "Viewport tool settings round trip");
        settings.SetInt("viewport.tool", 9);
        settings.SetFloat("viewport.snapRotate", -5.0f);
        const auto sanitized = LoadViewportToolPreferences(settings);
        Check(sanitized.tool == ViewportToolPreferences::Tool::Move && sanitized.rotateSnapDegrees == 15.0f, "Invalid values fall back");
        Check(IsSnapActive(true, false) && !IsSnapActive(true, true) && IsSnapActive(false, true), "Ctrl inverts snapping");
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
        UnsavedDocumentsResolve();
        SwitchGuardFollowsSelection();
        DuplicateNames();
        RecoverySessionMarker();
        ProjectScaffolding();
        ScriptSolutionGeneration();
        AssetHotReloadWatch();
        EditorCommandsRebindAndRun();
        ViewportNavigation();
        ViewportToolSettingsPersist();
    }
    catch (const std::exception &error)
    {
        std::cerr << "EditorWorkflowTests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "EditorWorkflowTests passed\n";
    return 0;
}
