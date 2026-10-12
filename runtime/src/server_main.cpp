#include "ServerOptions.h"
#include "ServerConsole.h"
#include "RuntimeContent.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/AssetDatabase.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/ScriptComponent.h"
#include "PlutoGE/scripting/ScriptEngine.h"
#include "PlutoGE/scripting/ScriptLogging.h"
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace
{
    volatile std::sig_atomic_t stopRequested = 0;
    void RequestStop(int) { stopRequested = 1; }
    bool DispatchServerCommand(PlutoGE::scene::Entity *entity, std::string_view command)
    {
        for (auto *script : entity->GetComponents<PlutoGE::scene::ScriptComponent>())
            if (script->OnServerCommand(command)) return true;
        for (auto *child : entity->GetChildren())
            if (DispatchServerCommand(child, command)) return true;
        return false;
    }
    std::filesystem::path ExecutablePath(const char *argument)
    {
#ifdef _WIN32
        std::wstring path(32768, L'\0');
        const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length && length < path.size()) { path.resize(length); return path; }
#else
        std::string path(4096, '\0');
        const auto length = readlink("/proc/self/exe", path.data(), path.size());
        if (length > 0 && static_cast<std::size_t>(length) < path.size()) { path.resize(length); return path; }
#endif
        return std::filesystem::absolute(argument);
    }
    void SetEnvironment(const std::string &name, const std::string &value)
    {
#ifdef _WIN32
        if (_putenv_s(name.c_str(), value.c_str()) != 0) throw std::runtime_error("Could not set server environment: " + name);
#else
        if (setenv(name.c_str(), value.c_str(), 1) != 0) throw std::runtime_error("Could not set server environment: " + name);
#endif
    }
    struct RuntimeLifetime
    {
        PlutoGE::core::Engine &engine;
        std::unique_ptr<PlutoGE::scene::Scene> scene;
        ~RuntimeLifetime()
        {
            engine.StopRuntime(); engine.SetScene(nullptr); scene.reset(); engine.Shutdown();
        }
    };
}

int main(int argc, char **argv)
{
    try
    {
        if (argc == 2 && std::string_view(argv[1]) == "--pack-version")
        {
            std::cout << PlutoGE::assets::kRuntimeContentPackMarker << '\n'
                      << PlutoGE::assets::kRuntimeAssetPipelineMarker << '\n'
                      << PlutoGE::assets::kHeadlessServerMarker << '\n';
            return 0;
        }
        if (argc > 1 && std::string_view(argv[1]) == "--export")
        {
            if (argc != 4) throw std::invalid_argument("Usage: PlutoGEServer --export <source.plutoproject> <output executable>");
            std::string error;
            const auto project = PlutoGE::assets::Project::Load(argv[2], &error);
            if (!project) throw std::runtime_error(error);
            PlutoGE::assets::ExportOptions options;
            options.pruneUnused = project->GetManifest().buildUsedAssetsOnly;
            options.alwaysInclude = project->GetManifest().buildAlwaysInclude;
            if (!PlutoGE::assets::ExportHeadlessProject(*project, argv[3], ExecutablePath(argv[0]), &error, options))
                throw std::runtime_error(error);
            std::cout << "Exported dedicated server: " << argv[3] << '\n';
            return 0;
        }
        auto options = PlutoGE::ServerOptions::Parse(argc, argv);
        if (options.project.empty())
        {
            options.project = ExecutablePath(argv[0]);
            options.project.replace_extension(".plutopack");
        }
        SetEnvironment("PLUTOGE_HEADLESS", "1");
        PlutoGE::ServerConsole console;
        console.StartPanel();
        std::signal(SIGINT, RequestStop);
        std::signal(SIGTERM, RequestStop);
        PlutoGE::scripting::SetScriptLogSink([](auto severity, std::string_view text)
        { std::clog << "[Script " << static_cast<int>(severity) << "] " << text << '\n'; });
        std::string error;
        PlutoGE::RuntimeContent content;
        const auto manifest = options.project.extension() == ".plutopack" ? content.MountProject(options.project) : options.project;
        const auto project = PlutoGE::assets::Project::Load(manifest, &error);
        if (!project) throw std::runtime_error(error);
        options.ApplyDefaults(project->GetManifest().server);
        auto effectiveManifest = project->GetManifest();
        effectiveManifest.server.startupScene = options.scene;
        effectiveManifest.server.scripts = options.scripts;
        effectiveManifest.server.tickRate = static_cast<int>(options.tickRate);
        PlutoGE::assets::Project effectiveProject(project->GetManifestPath(), effectiveManifest);
        if (!PlutoGE::assets::ValidateServerSettings(effectiveProject, &error)) throw std::runtime_error(error);
        // Deployment environment wins over authored defaults; explicit --env wins over both.
        for (const auto &variable : effectiveManifest.server.environment)
            if (!std::getenv(variable.name.c_str())) SetEnvironment(variable.name, variable.value);
        for (const auto &variable : options.environment) SetEnvironment(variable.name, variable.value);
        auto &engine = PlutoGE::core::Engine::GetInstance();
        auto &assets = engine.GetAssetManager();
        assets.SetProjectContext(project->GetRootDirectory().string(), project->GetManifest().assetDirectory,
                                 project->GetManifest().assetPipelineVersion);
        const auto catalog = project->GetRootDirectory() / "PlutoAssetCatalog.manifest";
        std::error_code catalogError;
        if (PlutoGE::content::IsRegularFile(catalog, catalogError))
        {
            if (!assets.LoadAssetCatalog(catalog.string(), &error)) throw std::runtime_error(error);
        }
        else
        {
            PlutoGE::assets::AssetDatabase database;
            PlutoGE::assets::AssetScanOptions scan;
            scan.createMissingMetadata = false; scan.hashContent = false; scan.collectDependencies = false;
            if (!database.Scan(*project, scan, &error)) throw std::runtime_error(error);
            assets.SetAssetSnapshot(database.GetCatalog(), database.GetStorageMap());
        }
        PlutoGE::core::EngineConfig config;
        config.headless = true; config.headlessScriptClasses = options.scripts;
        if (!engine.Initialize(config)) throw std::runtime_error("Headless engine initialization failed");
        RuntimeLifetime lifetime{engine, nullptr};
        const auto assembly = assets.ResolveAssetPath(project->GetManifest().scriptAssembly);
        if (!engine.GetScriptEngine().LoadAssembly(assembly))
            throw std::runtime_error(engine.GetScriptEngine().GetLastError());
        for (const auto &script : options.scripts)
            if (!engine.GetScriptEngine().HasClass(script)) throw std::runtime_error("Unknown server script: " + script);
        const auto scenePath = assets.ResolveAssetPath(options.scene);
        if (!engine.GetSceneLoading().Load(scenePath, [&](auto scene)
            { lifetime.scene = std::move(scene); engine.SetScene(lifetime.scene.get()); engine.StartRuntime(); }, {}))
            throw std::runtime_error(engine.GetSceneLoading().Status().error);
        std::cout << "Headless server running: " << options.scene << " at " << options.tickRate << " Hz\n";
        using Clock = std::chrono::steady_clock;
        const auto interval = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / options.tickRate));
        auto deadline = Clock::now();
        unsigned ticks = 0;
        std::cout << "Commands: help, status, quit (or stop/exit).\n";
        console.Prompt();
        while (!stopRequested && !engine.ConsumeApplicationQuitRequest() && (!options.ticks || ticks < options.ticks))
        {
            for (const auto &command : console.Poll())
            {
                if (command == "quit" || command == "stop" || command == "exit")
                {
                    stopRequested = 1;
                    break;
                }
                if (command == "help" || command == "?")
                {
                    std::cout << "help    Show commands\nstatus  Show scene, tick rate and elapsed simulation time\nquit    Shut down gracefully (aliases: stop, exit)\n";
                    for (auto *root : lifetime.scene->GetRootEntities())
                        if (DispatchServerCommand(root, "help")) break;
                }
                else if (command == "status")
                    std::cout << "Scene: " << options.scene << " | " << options.tickRate << " Hz | "
                              << ticks << " ticks | " << ticks / options.tickRate << " simulated seconds\n";
                else if (!command.empty())
                {
                    bool handled = false;
                    for (auto *root : lifetime.scene->GetRootEntities())
                        if (DispatchServerCommand(root, command)) { handled = true; break; }
                    if (!handled) std::cout << "Unknown command: " << command << ". Type help for commands.\n";
                }
                console.Prompt();
            }
            if (stopRequested) break;
            lifetime.scene->Update(1.0f / options.tickRate);
            ++ticks;
            if (const auto requested = engine.ConsumeSceneLoadRequest())
            {
                const auto reference = project->FindSceneAssetReference(*requested);
                const auto path = reference.empty() ? std::string{} : assets.ResolveAssetPath(reference);
                if (!engine.GetSceneLoading().Load(path, [&](auto nextScene)
                    {
                        engine.StopRuntime();
                        engine.SetScene(nullptr);
                        lifetime.scene.reset();
                        lifetime.scene = std::move(nextScene);
                        engine.SetScene(lifetime.scene.get());
                        engine.StartRuntime();
                        options.scene = reference;
                    }, {}))
                    std::cerr << "Scene transition failed: " << engine.GetSceneLoading().Status().error << '\n';
                deadline = Clock::now();
            }
            deadline += interval;
            // Bound catch-up work after a long stall rather than spinning indefinitely.
            if (Clock::now() - deadline > interval * 5) deadline = Clock::now();
            std::this_thread::sleep_until(deadline);
        }
        std::cout << "Headless server stopped after " << ticks << " ticks\n";
        return 0;
    }
    catch (const std::exception &error)
    { std::cerr << "Server: " << error.what() << '\n'; return 1; }
}
