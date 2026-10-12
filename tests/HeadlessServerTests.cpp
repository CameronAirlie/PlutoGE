#include "../runtime/src/ServerOptions.h"
#include "../runtime/src/ServerConsole.h"
#include "../runtime/src/RuntimeContent.h"
#include <fstream>
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/ScriptComponent.h"
#include "PlutoGE/scripting/ScriptEngine.h"
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
    int creates = 0, updates = 0, destroys = 0;
    struct SimulationScript : PlutoGE::scripting::ScriptInstance
    {
        void OnCreate() override { ++creates; }
        void OnUpdate(float) override { ++updates; }
        void OnDestroy() override { ++destroys; }
    };
}
int main(int argc, char **argv) try
{
    using namespace PlutoGE;
#ifdef _WIN32
    if (argc == 2 && std::string_view(argv[1]) == "--console-panel-test")
    {
        ServerConsolePanel panel;
        panel.Start();
        Check(panel.Active(), "Interactive console panel did not activate");
        for (int i = 0; i < 600; ++i) std::cout << "Server log " << i << '\n';
        panel.Draw("status");
        const auto screen = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        CONSOLE_SCREEN_BUFFER_INFO info{};
        Check(GetConsoleScreenBufferInfo(screen, &info) != 0, "Panel screen unavailable");
        const auto readRow = [&](SHORT row)
        {
            std::string text(info.srWindow.Right - info.srWindow.Left + 1, ' ');
            DWORD count = 0;
            ReadConsoleOutputCharacterA(screen, text.data(), static_cast<DWORD>(text.size()),
                {info.srWindow.Left, row}, &count);
            return text;
        };
        const auto commandBefore = readRow(info.srWindow.Bottom - 1);
        std::clog << "Live log while typing\n";
        panel.Draw("status");
        const auto commandAfter = readRow(info.srWindow.Bottom - 1);
        const auto lastLog = readRow(info.srWindow.Bottom - 3);
        const auto title = readRow(info.srWindow.Top);
        CloseHandle(screen);
        panel.Stop();
        Check(commandBefore.find("| > status") == 0 && commandBefore == commandAfter,
            "Logging overwrote the command box");
        Check(lastLog.find("Live log while typing") == 0, "Log tail did not scroll above command box");
        Check(title.find("DEDICATED SERVER") != std::string::npos, "Panel title missing");
        return 0;
    }
#endif
    Check(ServerConsole::Normalize(" \tSTATUS\r\n") == "status", "Console command normalization failed");
    Check(ServerConsole::Normalize(" \t") == "", "Blank console command was not ignored");
#ifdef _WIN32
    // An open pipe with only part of a command must not block the simulation.
    HANDLE consoleRead = nullptr, consoleWrite = nullptr;
    Check(CreatePipe(&consoleRead, &consoleWrite, nullptr, 0) != 0, "Console test pipe failed");
    const auto originalInput = GetStdHandle(STD_INPUT_HANDLE);
    SetStdHandle(STD_INPUT_HANDLE, consoleRead);
    ServerConsole console;
    DWORD written = 0;
    WriteFile(consoleWrite, "sta", 3, &written, nullptr);
    const auto partial = console.Poll();
    WriteFile(consoleWrite, "tus\r\nHELP\nquit", 14, &written, nullptr);
    const auto commands = console.Poll();
    CloseHandle(consoleWrite);
    const auto finalCommand = console.Poll();
    const auto afterEof = console.Poll();
    SetStdHandle(STD_INPUT_HANDLE, originalInput);
    CloseHandle(consoleRead);
    Check(partial.empty(), "Partial console input was dispatched");
    Check(commands == std::vector<std::string>{"status", "help"}, "Console line framing failed");
    Check(finalCommand == std::vector<std::string>{"quit"} && afterEof.empty(), "Console EOF handling failed");
#endif
    const char *arguments[] = {"server", "--project", "game.plutoproject", "--scene", "project://arena.plutoscene", "--script", "Test.Simulation", "--tick-rate", "60", "--ticks", "10"};
    auto options = ServerOptions::Parse(11, const_cast<char **>(arguments));
    Check(options.tickRate == 60 && options.ticks == 10 && options.scripts.size() == 1, "Server options parse failed");
    const char *invalid[] = {"server", "--tick-rate", "nan"};
    bool rejected = false;
    try { (void)ServerOptions::Parse(3, const_cast<char **>(invalid)); } catch (const std::invalid_argument &) { rejected = true; }
    Check(rejected, "Invalid server rate accepted");
    const auto contentRoot = std::filesystem::temp_directory_path() / ("PlutoGEHeadlessTest-" +
        std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(contentRoot / "source" / "Assets");
    std::ofstream(contentRoot / "source" / "Game.plutoproject") << "test manifest";
    std::ofstream(contentRoot / "source" / "Assets" / "test.dll") << "managed bytes";
    std::string packError;
    Check(content::WritePack(contentRoot / "source", contentRoot / "Game.plutopack", {}, &packError), "Test content pack write failed");
    std::filesystem::path mountedManifest;
    {
        RuntimeContent packed;
        mountedManifest = packed.MountProject(contentRoot / "Game.plutopack");
        std::string manifest;
        Check(content::ReadFile(mountedManifest, manifest) && manifest == "test manifest", "Packed project manifest missing");
        Check(std::filesystem::exists(mountedManifest.parent_path() / "Assets" / "test.dll"), "Managed pack files not materialized");
    }
    Check(!std::filesystem::exists(mountedManifest.parent_path()), "Temporary packed content leaked");
    std::ofstream(contentRoot / "Game.plutomounts") << "../outside.plutopack";
    rejected = false;
    try { RuntimeContent packed; (void)packed.MountProject(contentRoot / "Game.plutopack"); }
    catch (const std::runtime_error &) { rejected = true; }
    Check(rejected, "Unsafe patch path accepted");
    std::ofstream(contentRoot / "source" / "Assets" / "Server.plutoscene") << "SCENE\t1\n";
    std::ofstream(contentRoot / "source" / "Assets" / "Title.plutoscene") << "SCENE\t1\n";
    assets::ProjectManifest settingsManifest;
    settingsManifest.startupScene = "project://Title.plutoscene";
    settingsManifest.server.startupScene = "project://Server.plutoscene";
    settingsManifest.server.scripts = {"Test.Simulation"};
    settingsManifest.server.tickRate = 30;
    settingsManifest.server.outputExecutable = "Build/Server/Test Server.exe";
    settingsManifest.server.environment = {{"GAME_MODE", "team\tdeathmatch"}, {"GAME_PORT", "7777"}, {"GAME_EMPTY", ""}};
    assets::Project settingsProject(contentRoot / "source" / "Settings.plutoproject", settingsManifest);
    Check(settingsProject.Save(&packError), "Could not save server settings");
    const auto loaded = assets::Project::Load(settingsProject.GetManifestPath(), &packError);
    Check(loaded && loaded->GetManifest().server.tickRate == 30 && loaded->GetManifest().server.environment.size() == 3 &&
        loaded->GetManifest().server.environment[0].value == "team\tdeathmatch" &&
        loaded->GetManifest().server.environment[2].value.empty() && loaded->GetManifest().server.outputExecutable == settingsManifest.server.outputExecutable,
        "Server settings did not round trip");
    Check(assets::ValidateServerSettings(*loaded, &packError), "Valid server settings rejected");
    auto defaults = ServerOptions::Parse(1, const_cast<char **>(arguments));
    defaults.ApplyDefaults(loaded->GetManifest().server);
    Check(defaults.scene == settingsManifest.server.startupScene && defaults.tickRate == 30 && defaults.scripts.size() == 1,
        "Saved server defaults not applied");
    options.ApplyDefaults(loaded->GetManifest().server);
    Check(options.tickRate == 60 && options.scene == "project://arena.plutoscene", "CLI did not override saved server settings");
    const char *environmentArguments[] = {"server", "--env", "GAME_MODE=a=b"};
    auto overrides = ServerOptions::Parse(3, const_cast<char **>(environmentArguments));
    Check(overrides.environment[0].value == "a=b", "Environment value split incorrectly");
    const char *modeArguments[] = {"server", "--mode", "Defusal", "--map", "Foundry"};
    const auto modeOptions = ServerOptions::Parse(5, const_cast<char **>(modeArguments));
    Check(modeOptions.environment.size() == 2 && modeOptions.environment[0].name == "PLUTOGE_SERVER_MODE" &&
        modeOptions.environment[0].value == "Defusal" && modeOptions.environment[1].name == "PLUTOGE_SERVER_MAP" &&
        modeOptions.environment[1].value == "Foundry", "Mode/map CLI overrides lost");
    Check(!assets::IsServerEnvironmentName("PLUTOGE_HEADLESS") && !assets::IsServerEnvironmentName("1BAD") &&
        !assets::IsServerEnvironmentName("BAD=NAME"), "Invalid environment name accepted");
    settingsProject.GetManifest().server.scripts.push_back("Test.Simulation");
    Check(!assets::ValidateServerSettings(settingsProject, &packError), "Duplicate server scripts accepted");
    settingsProject.GetManifest().server.scripts.pop_back();
    settingsProject.GetManifest().server.environment.push_back({"PLUTOGE_HEADLESS", "0"});
    Check(!assets::ValidateServerSettings(settingsProject, &packError), "Reserved environment name accepted");
    settingsProject.GetManifest().server.environment.pop_back();
    std::filesystem::create_directories(contentRoot / "host");
    const auto host = contentRoot / "host" / "PlutoGEServer.exe";
    std::ofstream(host) << assets::kRuntimeContentPackMarker << '\n' << assets::kRuntimeAssetPipelineMarker << '\n' << assets::kHeadlessServerMarker;
    const auto exported = contentRoot / "deployment" / "Test Server.exe";
    Check(!assets::ExportHeadlessProject(settingsProject, contentRoot / "source" / "Server.exe", host, &packError),
        "Server export permitted destructive project-root output");
    std::vector<float> buildProgress;
    assets::ExportOptions progressOptions;
    progressOptions.pruneUnused = true;
    progressOptions.progress = [&](float value)
    {
        Check(value >= 0 && value <= 1 && (buildProgress.empty() || value >= buildProgress.back()),
            "Export progress must stay monotonic and within 0..1");
        buildProgress.push_back(value);
    };
    const bool serverExported = assets::ExportHeadlessProject(settingsProject, exported, host, &packError, progressOptions);
    Check(serverExported, packError.c_str());
    Check(buildProgress.size() >= 5 && buildProgress.back() == 1, "Export did not report stage progress/completion");
    Check(settingsProject.GetManifest().startupScene == "project://Title.plutoscene", "Export changed graphical startup scene");
    {
        RuntimeContent packed;
        const auto exportedManifest = packed.MountProject(assets::GetRuntimeContentPackPathForExecutable(exported));
        const auto exportedProject = assets::Project::Load(exportedManifest, &packError);
        Check(exportedProject && exportedProject->GetManifest().server.tickRate == 30 &&
            exportedProject->GetManifest().startupScene == "project://Server.plutoscene", "Export lost server settings/startup scene");
        std::string sceneText;
        Check(content::ReadFile(exportedProject->ResolveAssetReference("project://Server.plutoscene"), sceneText), "Cook omitted server startup scene");
        Check(!content::ReadFile(exportedProject->ResolveAssetReference("project://Title.plutoscene"), sceneText), "Server cook used graphical startup scene");
    }
    std::filesystem::remove_all(contentRoot);
    auto &engine = core::Engine::GetInstance();
    core::EngineConfig config; config.headless = true; config.headlessScriptClasses = {"Test.Simulation"};
    Check(engine.Initialize(config), "Headless initialization failed");
    Check(!engine.GetWindow().IsOpen() && !engine.GetRenderDevice() && !engine.GetSwapchain(), "Headless host initialized graphics");
    scripting::ScriptClassDefinition definition;
    definition.namespaceName = "Test"; definition.className = "Simulation";
    engine.GetScriptEngine().RegisterNativeClass(definition, [] { return std::make_unique<SimulationScript>(); });
    definition.className = "Presentation";
    engine.GetScriptEngine().RegisterNativeClass(definition, [] { return std::make_unique<SimulationScript>(); });
    auto scene = std::make_unique<scene::Scene>();
    auto entity = std::make_unique<scene::Entity>();
    entity->CreateComponent<scene::ColliderComponent>();
    entity->CreateComponent<scene::ScriptComponent>(scene::ScriptComponentConfig{.scriptClass = "Test.Simulation"});
    entity->CreateComponent<scene::ScriptComponent>(scene::ScriptComponentConfig{.scriptClass = "Test.Presentation"});
    scene->AddEntity(std::move(entity));
    engine.SetScene(scene.get());
    Check(engine.GetSceneLoading().Activate([&] { engine.StartRuntime(); }, {}), "Headless activation required presentation callback");
    for (int i = 0; i < 10; ++i) scene->Update(1.0f / 60);
    Check(creates == 1 && updates == 10, "Headless script allow list did not isolate simulation");
    scene::PhysicsRaycastHit hit;
    Check(scene->Raycast({0, 3, 0}, {0, -1, 0}, 5, hit), "Collision queries unavailable without renderer");
    engine.RequestApplicationQuit();
    Check(engine.ConsumeApplicationQuitRequest() && !engine.ConsumeApplicationQuitRequest(), "Headless quit request failed");
    engine.StopRuntime(); engine.SetScene(nullptr); scene.reset(); engine.Shutdown();
    Check(destroys == 1, "Headless script shutdown failed");
    std::cout << "PASS: server options, no window/device, simulation scripts, collision, presentation-free activation and shutdown\n";
    return 0;
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
