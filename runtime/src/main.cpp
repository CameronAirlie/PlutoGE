#include "PlutoGE/core/LoadingScreenSession.h"
#include "RuntimeProfiler.h"
#include "ProjectBenchmark.h"
#include <optional>
#include "PlutoGE/platform/ContentPack.h"
#include "PlutoGE/render/SceneEnvironment.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/SpatialUpscaler.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <glm/gtc/matrix_inverse.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

namespace PlutoGE
{
    namespace
    {
        struct TemporaryContentDirectory
        {
            std::filesystem::path path;

            ~TemporaryContentDirectory()
            {
                content::UnmountAll();
                if (!path.empty())
                {
                    std::error_code errorCode;
                    std::filesystem::remove_all(path, errorCode);
                }
            }
        };

#ifdef _WIN32
        struct RuntimeDiagnostics
        {
            std::ofstream logFile;
            std::string currentPhase = "startup";

            void Initialize(const std::filesystem::path &executablePath)
            {
                logFile.open(executablePath.parent_path() / "PlutoGERuntime.log", std::ios::out | std::ios::trunc);
            }

            void Log(const std::string &message)
            {
                if (!logFile.is_open())
                {
                    return;
                }

                logFile << message << std::endl;
                logFile.flush();
            }
        };

        RuntimeDiagnostics g_runtimeDiagnostics;

        LONG WINAPI RuntimeUnhandledExceptionFilter(EXCEPTION_POINTERS *exceptionPointers)
        {
            if (exceptionPointers)
            {
                g_runtimeDiagnostics.Log("Unhandled exception during phase: " + g_runtimeDiagnostics.currentPhase);
                g_runtimeDiagnostics.Log("Exception code: 0x" + std::to_string(static_cast<unsigned long long>(exceptionPointers->ExceptionRecord->ExceptionCode)));
            }
            else
            {
                g_runtimeDiagnostics.Log("Unhandled exception with no exception record during phase: " + g_runtimeDiagnostics.currentPhase);
            }

            return EXCEPTION_EXECUTE_HANDLER;
        }
#endif

        void CollectEntitiesRecursive(scene::Entity *entity, std::vector<scene::Entity *> &entities)
        {
            if (!entity)
            {
                return;
            }

            entities.push_back(entity);
            for (auto *child : entity->GetChildren())
            {
                CollectEntitiesRecursive(child, entities);
            }
        }

        scene::CameraComponent *FindFirstSceneCamera(scene::Scene *scene)
        {
            if (!scene)
            {
                return nullptr;
            }

            std::vector<scene::Entity *> entities;
            for (auto *rootEntity : scene->GetRootEntities())
            {
                CollectEntitiesRecursive(rootEntity, entities);
            }

            scene::CameraComponent *fallbackCamera = nullptr;

            for (auto *entity : entities)
            {
                if (!entity || !entity->IsActive())
                {
                    continue;
                }

                if (auto *cameraComponent = entity->GetComponent<scene::CameraComponent>())
                {
                    if (!cameraComponent->GetCamera() || !cameraComponent->IsEnabled())
                    {
                        continue;
                    }

                    if (cameraComponent->IsMainCamera())
                    {
                        return cameraComponent;
                    }

                    if (!fallbackCamera)
                    {
                        fallbackCamera = cameraComponent;
                    }
                }
            }

            return fallbackCamera;
        }

        std::filesystem::path ResolveExecutablePath(char **argv)
        {
            if (argv && argv[0] && argv[0][0] != '\0')
            {
                std::error_code errorCode;
                return std::filesystem::absolute(argv[0], errorCode).lexically_normal();
            }

            return std::filesystem::current_path() / "PlutoGERuntime";
        }

#ifdef _WIN32
        std::string FormatVec3(const glm::vec3 &value)
        {
            return std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z);
        }

        void LogSceneDiagnostics(scene::Scene *scene, scene::CameraComponent *cameraComponent, render::Renderer &renderer)
        {
            if (!scene)
            {
                return;
            }

            std::vector<scene::Entity *> entities;
            for (auto *rootEntity : scene->GetRootEntities())
            {
                CollectEntitiesRecursive(rootEntity, entities);
            }

            std::size_t activeEntityCount = 0;
            std::size_t meshComponentCount = 0;
            std::size_t activeMeshComponentCount = 0;
            std::size_t loadedMeshCount = 0;

            g_runtimeDiagnostics.Log("Scene light count: " + std::to_string(scene->GetLights().size()));
            g_runtimeDiagnostics.Log(std::string("Scene environment map: ") + (scene->GetEnvironmentMapPath().empty() ? "<none>" : scene->GetEnvironmentMapPath()));
            g_runtimeDiagnostics.Log(std::string("Scene environment path exists: ") + (scene->GetEnvironmentMapPath().empty() ? "no" : (PlutoGE::content::Exists(scene->GetEnvironmentMapPath()) ? "yes" : "no")));
            g_runtimeDiagnostics.Log(std::string("Scene environment texture loaded: ") + (scene->GetEnvironmentMapTexture() ? "yes" : "no"));
            g_runtimeDiagnostics.Log("Scene environment intensity: " + std::to_string(scene->GetEnvironmentIntensity()));

            if (cameraComponent && cameraComponent->GetOwner() && cameraComponent->GetCamera())
            {
                auto *cameraOwner = cameraComponent->GetOwner();
                g_runtimeDiagnostics.Log("Main camera entity: " + cameraOwner->GetName());
                g_runtimeDiagnostics.Log("Main camera position: " + FormatVec3(cameraOwner->GetWorldPosition()));
                g_runtimeDiagnostics.Log("Main camera rotation: " + FormatVec3(cameraOwner->GetWorldRotation()));
                g_runtimeDiagnostics.Log("Main camera scale: " + FormatVec3(cameraOwner->GetWorldScale()));
                g_runtimeDiagnostics.Log("Main camera FOV: " + std::to_string(cameraComponent->GetCamera()->GetFOV()));
            }

            for (auto *entity : entities)
            {
                if (!entity)
                {
                    continue;
                }

                if (entity->IsActive())
                {
                    ++activeEntityCount;
                }

                if (auto *meshComponent = entity->GetComponent<scene::MeshComponent>())
                {
                    ++meshComponentCount;
                    if (entity->IsActive() && meshComponent->IsEnabled())
                    {
                        ++activeMeshComponentCount;
                    }
                    if (meshComponent->GetMesh())
                    {
                        ++loadedMeshCount;
                    }

                    g_runtimeDiagnostics.Log(
                        "Mesh entity: " + entity->GetName() + " active=" + std::string(entity->IsActive() ? "yes" : "no") + " componentEnabled=" + std::string(meshComponent->IsEnabled() ? "yes" : "no") + " meshLoaded=" + std::string(meshComponent->GetMesh() ? "yes" : "no") + " source=" + (meshComponent->GetSourceMeshPath().empty() ? std::string("<none>") : meshComponent->GetSourceMeshPath()) + " position=" + FormatVec3(entity->GetWorldPosition()));
                }
            }

            g_runtimeDiagnostics.Log("Scene active entities: " + std::to_string(activeEntityCount));
            g_runtimeDiagnostics.Log("Scene mesh components: " + std::to_string(meshComponentCount));
            g_runtimeDiagnostics.Log("Scene active mesh components: " + std::to_string(activeMeshComponentCount));
            g_runtimeDiagnostics.Log("Scene loaded meshes: " + std::to_string(loadedMeshCount));
            g_runtimeDiagnostics.Log("Queued render commands: " + std::to_string(renderer.GetQueuedRenderCommandCount()));
        }
#endif
    }
}

int RunRuntime(int argc, char **argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--pack-version")
    {
        std::cout << PlutoGE::assets::kRuntimeContentPackMarker << '\n';
        return 0;
    }
    if (argc > 1 && std::string_view(argv[1]) == "--pack")
    {
        if (argc != 4) { std::cerr << "Usage: --pack <content directory> <output.plutopack>\n"; return 2; }
        std::string error;
        if (!PlutoGE::content::WritePack(argv[2], argv[3], {}, &error)) { std::cerr << error << '\n'; return 1; }
        return 0;
    }
    if (argc > 1 && std::string_view(argv[1]) == "--verify-pack")
    {
        if (argc != 3) { std::cerr << "Usage: --verify-pack <file.plutopack>\n"; return 2; }
        std::string error;
        auto pack = PlutoGE::content::Pack::Open(argv[2], &error);
        if (!pack || !pack->Verify(&error)) { std::cerr << error << '\n'; return 1; }
        std::cout << "Verified " << pack->Files().size() << " packed files.\n";
        return 0;
    }
    if (argc > 1 && std::string_view(argv[1]) == "--export")
    {
        if (argc < 4)
        {
            std::cerr << "Usage: PlutoGERuntime --export <project.plutoproject> <output executable> [--prune] [--include project://asset] [--no-compression]" << std::endl;
            return 2;
        }

        PlutoGE::assets::ExportOptions exportOptions;
        for (int i = 4; i < argc; ++i)
        {
            const std::string_view option(argv[i]);
            if (option == "--prune") exportOptions.pruneUnused = true;
            else if (option == "--no-compression") exportOptions.compress = false;
            else if (option == "--include" && i + 1 < argc) exportOptions.alwaysInclude.emplace_back(argv[++i]);
            else { std::cerr << "Unknown or incomplete export option: " << option << '\n'; return 2; }
        }
        std::string exportError;
        const auto sourceProject = PlutoGE::assets::Project::Load(std::filesystem::path(argv[2]), &exportError);
        if (!sourceProject)
        {
            std::cerr << (exportError.empty() ? "Failed to load the project for export." : exportError) << std::endl;
            return 1;
        }

        const auto exporterExecutable = PlutoGE::ResolveExecutablePath(argv);
        if (!PlutoGE::assets::ExportStandaloneProject(*sourceProject,
                                                       std::filesystem::path(argv[3]),
                                                       exporterExecutable,
                                                       &exportError, exportOptions))
        {
            std::cerr << (exportError.empty() ? "Failed to export the game." : exportError) << std::endl;
            return 1;
        }

        std::cout << "Exported game to " << std::filesystem::absolute(argv[3]).lexically_normal().string() << std::endl;
        return 0;
    }

    if (argc > 2 && std::string_view(argv[1]) == "--import-bench")
    {
        const std::filesystem::path meshPath = std::filesystem::absolute(argv[2]).lexically_normal();
        auto &engine = PlutoGE::core::Engine::GetInstance();
        const auto importStart = std::chrono::high_resolution_clock::now();
        const auto importedMeshSourceAsset = engine.GetMeshImporter().ImportMeshSourceAsset(meshPath.string());
        const auto elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - importStart).count();

        std::cout
            << "Import bench for '" << meshPath.string() << "': "
            << elapsedMs << "ms, "
            << importedMeshSourceAsset.meshData.vertices.size() << " vertices, "
            << importedMeshSourceAsset.meshData.indices.size() << " indices, "
            << importedMeshSourceAsset.materials.size() << " materials, "
            << importedMeshSourceAsset.textures.size() << " textures"
            << std::endl;
        return 0;
    }

    if (argc > 2 && std::string_view(argv[1]) == "--import-bench-full")
    {
        const std::filesystem::path meshPath = std::filesystem::absolute(argv[2]).lexically_normal();
        auto &engine = PlutoGE::core::Engine::GetInstance();
        PlutoGE::core::EngineConfig config{
            PlutoGE::platform::WindowConfig{
                .title = "PlutoGE Import Bench",
                .width = 64,
                .height = 64,
                .resizable = false,
                .visible = false,
                .fullscreen = false,
            }};

        if (!engine.Initialize(config))
        {
            std::cerr << "Failed to initialize engine for full import benchmark." << std::endl;
            return 1;
        }

        const auto importStart = std::chrono::high_resolution_clock::now();
        const auto importedMeshAsset = engine.ImportMeshAsset(meshPath.string());
        const auto elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - importStart).count();

        std::cout
            << "Full import bench for '" << meshPath.string() << "': "
            << elapsedMs << "ms, "
            << importedMeshAsset.materials.size() << " materials"
            << std::endl;

        engine.Shutdown();
        return importedMeshAsset.mesh ? 0 : 1;
    }

    const bool projectBenchmarkEnabled = argc > 1 && std::string_view(argv[1]) == "--benchmark-project";
    std::optional<PlutoGE::ProjectBenchmarkOptions> projectBenchmark;
    if (projectBenchmarkEnabled)
    {
        try { projectBenchmark = PlutoGE::ProjectBenchmarkOptions::Parse({argv + 2, static_cast<std::size_t>(argc - 2)}); }
        catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 2; }
    }
    const std::size_t benchmarkWarmupFrames = projectBenchmark ? projectBenchmark->warmup : 120;
    constexpr std::size_t defaultBenchmarkFrames = 600;
    const bool profilerEnabled = argc > 1 && std::string_view(argv[1]) == "--profiler";
    const bool benchmarkEnabled = projectBenchmarkEnabled || (argc > 1 && std::string_view(argv[1]) == "--benchmark");
    std::size_t benchmarkFrameCount = projectBenchmark ? projectBenchmark->frames : defaultBenchmarkFrames;
    if (benchmarkEnabled && !projectBenchmark && argc > 2)
    {
        try
        {
            benchmarkFrameCount = std::max<std::size_t>(1, std::stoull(argv[2]));
        }
        catch (const std::exception &)
        {
            std::cerr << "Invalid benchmark frame count: " << argv[2] << std::endl;
            return 2;
        }
    }

    const auto executablePath = PlutoGE::ResolveExecutablePath(argv);
    PlutoGE::TemporaryContentDirectory temporaryContent;
    auto manifestPath = projectBenchmark ? projectBenchmark->project : argc > 1 && !benchmarkEnabled && !profilerEnabled
                            ? std::filesystem::path(argv[1])
                            : PlutoGE::assets::GetRuntimeManifestPathForExecutable(executablePath);

    if (!projectBenchmark && (argc <= 1 || benchmarkEnabled || profilerEnabled))
    {
        const auto contentPackPath = PlutoGE::assets::GetRuntimeContentPackPathForExecutable(executablePath);
        if (std::filesystem::exists(contentPackPath))
        {
            std::error_code temporaryError;
            const auto temporaryRoot = std::filesystem::temp_directory_path(temporaryError);
            const auto uniqueName = executablePath.stem().string() + "-" +
                                    std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
            const auto candidate = temporaryRoot / "PlutoGE" / "Content" / uniqueName;
            if (!temporaryError) std::filesystem::create_directories(candidate.parent_path(), temporaryError);
            if (temporaryError || !std::filesystem::create_directory(candidate, temporaryError))
            {
                std::cerr << "Cannot reserve runtime content directory: " << temporaryError.message() << '\n';
                return 1;
            }
            temporaryContent.path = candidate;
            std::string unpackError;
            if (temporaryError || !PlutoGE::content::Mount(contentPackPath, temporaryContent.path, &unpackError))
            {
                std::cerr << (unpackError.empty() ? "Failed to mount the game content pack." : unpackError) << std::endl;
                return 1;
            }
            // Explicit ordered list avoids silently mounting arbitrary sidecar archives.
            auto mountsPath = executablePath; mountsPath.replace_extension(".plutomounts");
            std::ifstream mounts(mountsPath);
            std::string packName;
            while (std::getline(mounts, packName))
            {
                if (!packName.empty() && packName.back() == '\r') packName.pop_back();
                if (packName.empty() || packName.front() == '#') continue;
                const auto relative = std::filesystem::u8path(packName);
                if (relative.has_parent_path() || relative.is_absolute() || relative.extension() != ".plutopack" ||
                    !PlutoGE::content::Mount(executablePath.parent_path() / relative, temporaryContent.path, &unpackError))
                { std::cerr << "Cannot mount patch pack " << packName << ": " << unpackError << '\n'; return 1; }
            }
            // Managed dependencies, ProjectStorage JSON data, and InputActionMap's
            // System.IO loader require disk paths. Native readers use the pack directly.
            for (const auto &path : PlutoGE::content::Files(temporaryContent.path))
            {
                auto extension = path.extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (extension == ".dll" || extension == ".pdb" || extension == ".plutoinput" || extension == ".json")
                    if (!PlutoGE::content::Materialize(path, &unpackError)) { std::cerr << unpackError << '\n'; return 1; }
            }
            manifestPath = temporaryContent.path / PlutoGE::assets::GetRuntimeManifestPathForExecutable(executablePath).filename();
        }
    }

#ifdef _WIN32
    PlutoGE::g_runtimeDiagnostics.Initialize(executablePath);
    PlutoGE::g_runtimeDiagnostics.Log("Runtime start");
    PlutoGE::g_runtimeDiagnostics.Log("Executable: " + executablePath.string());
    PlutoGE::g_runtimeDiagnostics.Log("Manifest: " + manifestPath.string());
    SetUnhandledExceptionFilter(PlutoGE::RuntimeUnhandledExceptionFilter);
#endif

    std::string errorMessage;
#ifdef _WIN32
    PlutoGE::g_runtimeDiagnostics.currentPhase = "load project manifest";
#endif
    auto project = PlutoGE::assets::Project::Load(manifestPath, &errorMessage);
    if (!project)
    {
#ifdef _WIN32
        PlutoGE::g_runtimeDiagnostics.Log("Failed to load project manifest: " + errorMessage);
#endif
        std::cerr << (errorMessage.empty() ? "Failed to load runtime project manifest." : errorMessage) << std::endl;
        return 1;
    }

    auto &engine = PlutoGE::core::Engine::GetInstance();
    engine.GetAssetManager().SetProjectContext(project->GetRootDirectory().string(), project->GetManifest().assetDirectory);

#ifdef _WIN32
    PlutoGE::g_runtimeDiagnostics.Log("Project root: " + project->GetRootDirectory().string());
    PlutoGE::g_runtimeDiagnostics.currentPhase = "engine initialize";
#endif

    PlutoGE::core::EngineConfig config{
        PlutoGE::platform::WindowConfig{
            .title = project->GetManifest().windowTitle.empty() ? project->GetManifest().name : project->GetManifest().windowTitle,
            .width = project->GetManifest().windowWidth,
            .height = project->GetManifest().windowHeight,
            .resizable = true,
            .visible = !projectBenchmarkEnabled,
            .fullscreen = false,
        }};
    config.vSync = projectBenchmark ? false : project->GetManifest().vSyncEnabled;
    config.enableProfiling = profilerEnabled;
    config.graphicsApi = project->GetManifest().graphicsApi;
    config.temporalUpscaler = project->GetManifest().GetTemporalUpscalerOptions();

    if (!engine.Initialize(config))
    {
#ifdef _WIN32
        PlutoGE::g_runtimeDiagnostics.Log("Engine initialization failed");
#endif
        std::cerr << "Failed to initialize runtime engine." << std::endl;
        return 1;
    }

    if (config.graphicsApi == PlutoGE::render::rhi::GraphicsApi::OpenGL)
        engine.GetRenderer().SetVSyncEnabled(config.vSync);
    if (projectBenchmark && engine.GetRenderDevice())
        engine.GetRenderDevice()->GetImmediateContext().SetGpuProfilingEnabled(true);

    if (!project->GetManifest().scriptAssembly.empty())
    {
        const std::string scriptAssemblyPath = engine.GetAssetManager().ResolveAssetPath(project->GetManifest().scriptAssembly);
        if (scriptAssemblyPath.empty() || !std::filesystem::exists(scriptAssemblyPath) || !engine.GetScriptEngine().LoadAssembly(scriptAssemblyPath))
        {
            std::string scriptError;
            if (scriptAssemblyPath.empty())
            {
                scriptError = "The configured assembly path could not be resolved: " + project->GetManifest().scriptAssembly;
            }
            else if (!std::filesystem::exists(scriptAssemblyPath))
            {
                scriptError = "The script assembly does not exist: " + scriptAssemblyPath;
            }
            else
            {
                scriptError = engine.GetScriptEngine().GetLastError();
                if (scriptError.empty())
                {
                    scriptError = "The managed runtime rejected the script assembly: " + scriptAssemblyPath;
                }
            }
#ifdef _WIN32
            PlutoGE::g_runtimeDiagnostics.Log("Failed to load script assembly: " + scriptError);
#endif
            std::cerr << "Failed to load project script assembly: " << scriptError << std::endl;
            engine.Shutdown();
            return 1;
        }
    }

#ifdef _WIN32
    PlutoGE::g_runtimeDiagnostics.currentPhase = "resolve startup scene";
#endif
    const std::string startupScenePath = engine.GetAssetManager().ResolveAssetPath(project->GetManifest().startupScene);
    if (startupScenePath.empty())
    {
#ifdef _WIN32
        PlutoGE::g_runtimeDiagnostics.Log("Startup scene path was empty");
#endif
        std::cerr << "Project manifest does not define a valid startup scene." << std::endl;
        engine.Shutdown();
        return 1;
    }

#ifdef _WIN32
    PlutoGE::g_runtimeDiagnostics.Log("Startup scene: " + startupScenePath);
    PlutoGE::g_runtimeDiagnostics.currentPhase = "load startup scene";
#endif

    std::unique_ptr<PlutoGE::scene::Scene> scene;
    const auto loadWithScreen = [&](const std::string &path, const PlutoGE::core::SceneLoading::Activation &activate)
    {
        PlutoGE::core::LoadingScreenSession loading(engine, project->GetManifest().loadingScreen);
        return engine.GetSceneLoading().Load(path, activate,
            [&](const PlutoGE::core::SceneLoadStatus &status) { loading.Present(status); });
    };
    if (!loadWithScreen(startupScenePath, [&](auto loaded)
        {
            scene = std::move(loaded);
            engine.SetScene(scene.get());
            engine.StartRuntime();
        }))
    {
        std::cerr << engine.GetSceneLoading().Status().error << std::endl;
        engine.StopRuntime();
        engine.SetScene(nullptr);
        scene.reset();
        engine.Shutdown();
        return 1;
    }

#ifdef _WIN32
    std::vector<PlutoGE::scene::Entity *> loadedEntities;
    for (auto *rootEntity : scene->GetRootEntities())
    {
        PlutoGE::CollectEntitiesRecursive(rootEntity, loadedEntities);
    }
    PlutoGE::g_runtimeDiagnostics.Log("Loaded entity count: " + std::to_string(loadedEntities.size()));
    PlutoGE::g_runtimeDiagnostics.Log(std::string("Startup camera present: ") + (PlutoGE::FindFirstSceneCamera(scene.get()) ? "yes" : "no"));
#endif

    auto lastFrameTime = std::chrono::high_resolution_clock::now();
    auto &renderer = engine.GetRenderer();
    auto &window = engine.GetWindow();
    const auto &runtimeManifest = project->GetManifest();
    const bool useVulkanRenderer = config.graphicsApi == PlutoGE::render::rhi::GraphicsApi::Vulkan;
    const float runtimeRenderScale = std::clamp(runtimeManifest.runtimeRenderScale, 0.5f, 1.0f);
    const bool runtimeUpscalingEnabled =
        !useVulkanRenderer && runtimeManifest.runtimeUpscaler == PlutoGE::assets::RuntimeUpscalerMode::Spatial &&
        runtimeRenderScale < 0.999f;
    std::unique_ptr<PlutoGE::render::RenderTarget> runtimeRenderTarget;
    PlutoGE::render::SpatialUpscaler runtimeUpscaler;
    bool hasLoggedFirstFrame = false;
    bool hasLoggedFirstFrameDiagnostics = false;
    std::size_t benchmarkFrameIndex = 0;
    std::vector<double> benchmarkFrameTimes;
    std::vector<PlutoGE::ProjectBenchmarkSample> projectBenchmarkSamples;
    PlutoGE::ProjectBenchmarkGpuScopes projectBenchmarkGpuScopes;
    if (projectBenchmark) projectBenchmarkSamples.reserve(benchmarkFrameCount);
    if (benchmarkEnabled)
        benchmarkFrameTimes.reserve(benchmarkFrameCount);

    std::unique_ptr<PlutoGE::RuntimeProfiler> runtimeProfiler;
    if (profilerEnabled)
    {
        runtimeProfiler = std::make_unique<PlutoGE::RuntimeProfiler>(renderer);
        if (!runtimeProfiler->Initialize())
        {
            std::cerr << "Failed to initialize runtime profiler window." << std::endl;
            runtimeProfiler.reset();
            engine.StopRuntime();
            engine.Shutdown();
            return 1;
        }
    }

    while (!window.ShouldClose())
    {
        PlutoGE::core::CpuTrace cpuTrace(runtimeProfiler && runtimeProfiler->profiler.IsRecording());
        PlutoGE::ui::EditorFrameTimingStats frameTiming;
        const auto frameStart = std::chrono::high_resolution_clock::now();
        window.PollEvents();
        const auto currentFrameTime = std::chrono::high_resolution_clock::now();
        const float deltaTime = projectBenchmark ? PlutoGE::ProjectBenchmarkOptions::FixedDelta : std::chrono::duration<float>(currentFrameTime - lastFrameTime).count();
        lastFrameTime = currentFrameTime;

#ifdef _WIN32
        PlutoGE::g_runtimeDiagnostics.currentPhase = "scene update";
#endif
        if (runtimeProfiler) renderer.BeginProfilingFrame();
        if (scene)
        {
            PlutoGE::core::CpuScope scope("Runtime.SceneUpdate", PlutoGE::core::CpuCategory::Other);
            scene->Update(deltaTime);
        }
        const auto updateEnd = std::chrono::high_resolution_clock::now();
        PlutoGE::core::CpuScope renderScope("Runtime.Render", PlutoGE::core::CpuCategory::Rendering);
        frameTiming.sceneUpdateMs = std::chrono::duration<float, std::milli>(updateEnd - currentFrameTime).count();

        if (const auto requestedScene = engine.ConsumeSceneLoadRequest())
        {
            const std::string reference = project->FindSceneAssetReference(*requestedScene);
            const std::string requestedPath = reference.empty() ? std::string{} : engine.GetAssetManager().ResolveAssetPath(reference);
            if (!loadWithScreen(requestedPath, [&](auto nextScene)
                {
                    auto previousScene = std::move(scene);
                    scene = std::move(nextScene);
                    engine.SetScene(scene.get());
                }))
                std::cerr << "Scene transition failed: " << engine.GetSceneLoading().Status().error << std::endl;
            // Loading uses wall time; do not feed that time into the next physics step.
            lastFrameTime = std::chrono::high_resolution_clock::now();
            renderer.ClearRenderCommands();
            // Keep the loading frame visible until the new scene has submitted
            // its first frame's commands; never present an empty transition frame.
            continue;
        }

#ifdef _WIN32
        if (!hasLoggedFirstFrameDiagnostics)
        {
            PlutoGE::LogSceneDiagnostics(scene.get(), PlutoGE::FindFirstSceneCamera(scene.get()), renderer);
            hasLoggedFirstFrameDiagnostics = true;
        }
#endif

#ifdef _WIN32
        PlutoGE::g_runtimeDiagnostics.currentPhase = "begin frame";
#endif
        PlutoGE::render::RenderTarget *frameRenderTarget = nullptr;
        const auto windowExtents = window.GetExtents();
        if (runtimeUpscalingEnabled && windowExtents.width > 0 && windowExtents.height > 0)
        {
            const int internalWidth = (std::max)(1, static_cast<int>(std::lround(windowExtents.width * runtimeRenderScale)));
            const int internalHeight = (std::max)(1, static_cast<int>(std::lround(windowExtents.height * runtimeRenderScale)));
            if (!runtimeRenderTarget)
            {
                runtimeRenderTarget = std::make_unique<PlutoGE::render::RenderTarget>(
                    PlutoGE::render::RenderTargetConfig{.width = internalWidth, .height = internalHeight});
            }
            else if (runtimeRenderTarget->GetWidth() != internalWidth || runtimeRenderTarget->GetHeight() != internalHeight)
            {
                runtimeRenderTarget->Resize(internalWidth, internalHeight);
            }

            if (runtimeRenderTarget->IsInitialized())
                frameRenderTarget = runtimeRenderTarget.get();
        }

        if (useVulkanRenderer)
        {
            if (auto *cameraComponent = PlutoGE::FindFirstSceneCamera(scene.get());
                cameraComponent && windowExtents.width > 0 && windowExtents.height > 0)
            {
#ifdef _WIN32
                PlutoGE::g_runtimeDiagnostics.currentPhase = "render Vulkan frame";
#endif
                const auto cameraData = cameraComponent->GetCameraData(windowExtents.width, windowExtents.height);
                const auto lighting = PlutoGE::render::BuildSceneLighting(cameraData, scene.get());
                std::vector<PlutoGE::render::IPostProcessEffect *> postProcessEffects;
                postProcessEffects.reserve(cameraComponent->GetPostProcessEffects().size());
                for (const auto &effect : cameraComponent->GetPostProcessEffects())
                    postProcessEffects.push_back(effect.get());
                const auto readTexturePixels = [](const PlutoGE::render::Texture &texture)
                {
                    const auto source = texture.GetRgba8Pixels();
                    return std::vector<std::byte>(reinterpret_cast<const std::byte *>(source.data()),
                                                  reinterpret_cast<const std::byte *>(source.data() + source.size()));
                };
                if (!engine.GetRhiRenderService().RenderSceneAndPresent(
                        cameraData, lighting, renderer.GetSceneRenderCommands(), readTexturePixels, scene.get(), postProcessEffects))
                {
                    std::cerr << "Failed to render the Vulkan runtime frame." << std::endl;
                    window.RequestClose();
                }
            }
            renderer.ClearRenderCommands();
        }
        else
        {
            renderer.BeginFrame(frameRenderTarget);
            if (auto *cameraComponent = PlutoGE::FindFirstSceneCamera(scene.get()))
            {
#ifdef _WIN32
                PlutoGE::g_runtimeDiagnostics.currentPhase = "render frame";
#endif
                renderer.RenderFrame(*cameraComponent, frameRenderTarget, scene->GetLights());
            }
            renderer.ClearRenderCommands();

            if (frameRenderTarget)
            {
                renderer.EndFrame(frameRenderTarget);
                if (!runtimeUpscaler.UpscaleToFramebuffer(
                        *frameRenderTarget, windowExtents.width, windowExtents.height,
                        {.sharpness = runtimeManifest.runtimeUpscaleSharpness}))
                {
                    PlutoGE::render::Graphics::BindFramebuffer(GL_READ_FRAMEBUFFER, frameRenderTarget->GetFramebufferID());
                    PlutoGE::render::Graphics::BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
                    glBlitFramebuffer(0, 0, frameRenderTarget->GetWidth(), frameRenderTarget->GetHeight(),
                                      0, 0, windowExtents.width, windowExtents.height,
                                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
                    PlutoGE::render::Graphics::BindFramebuffer(GL_FRAMEBUFFER, 0);
                }
            }

#ifdef _WIN32
            PlutoGE::g_runtimeDiagnostics.currentPhase = "end frame";
#endif
            renderer.EndFrame();
        }

        renderScope.End();
        if (runtimeProfiler)
        {
            const auto frameEnd = std::chrono::high_resolution_clock::now();
            frameTiming.viewportRenderMs = std::chrono::duration<float, std::milli>(frameEnd - updateEnd).count();
            frameTiming.eventPollingMs = std::chrono::duration<float, std::milli>(currentFrameTime - frameStart).count();
            frameTiming.gameViewportWidth = windowExtents.width;
            frameTiming.gameViewportHeight = windowExtents.height;
            frameTiming.renderedViewportCount = 1;
            frameTiming.renderedViewportPixels = std::uint64_t((std::max)(0, windowExtents.width)) * (std::max)(0, windowExtents.height);
            frameTiming.vSyncEnabled = config.vSync;
            if (useVulkanRenderer)
            {
                frameTiming.rhiTimingStats = engine.GetRenderDevice()->GetTimingStats("Scene");
                frameTiming.rhiSceneTimingStats = engine.GetRhiRenderService().GetTimingStats();
            }
            if (scene)
            {
                const auto &update = scene->GetUpdateTimingStats();
                frameTiming.scenePreparationMs = update.preparationMs;
                frameTiming.sceneRuntimeUiMs = update.runtimeUiMs;
                frameTiming.sceneComponentsMs = update.componentsMs;
                frameTiming.sceneLateScriptsMs = update.lateScriptsMs;
                frameTiming.sceneAudioMs = update.audioMs;
                frameTiming.sceneRenderSubmissionMs = update.renderSubmissionMs;
                frameTiming.sceneMeshSubmissionMs = update.meshSubmissionMs;
                frameTiming.sceneTerrainSubmissionMs = update.terrainSubmissionMs;
                frameTiming.sceneFoliageSubmissionMs = update.foliageSubmissionMs;
                frameTiming.scenePhysicsMs = update.physicsMs;
                frameTiming.componentTimings = update.componentTimings;
                frameTiming.animationTimings = update.animationTimings;
                frameTiming.scriptUpdateTimings = update.scriptUpdateTimings;
                frameTiming.scriptLateUpdateTimings = update.scriptLateUpdateTimings;
            }
            runtimeProfiler->profiler.CompleteFrame(
                std::chrono::duration<float, std::milli>(frameEnd - frameStart).count(),
                frameTiming, {}, renderer, PlutoGE::render::RmlUiRuntime::Get().GetCpuTiming(),
                cpuTrace.TakeSamples(), cpuTrace.GetDroppedCount());
            runtimeProfiler->Draw();
        }

        if (benchmarkEnabled)
        {
            if (benchmarkFrameIndex >= benchmarkWarmupFrames)
            {
                if (projectBenchmark)
                {
                    const auto end = std::chrono::high_resolution_clock::now();
                    PlutoGE::ProjectBenchmarkSample sample;
                    sample.frameMs = std::chrono::duration<double, std::milli>(end - frameStart).count();
                    sample.updateMs = frameTiming.sceneUpdateMs;
                    sample.renderPresentMs = std::chrono::duration<double, std::milli>(end - updateEnd).count();
                    sample.uiMs = PlutoGE::render::RmlUiRuntime::Get().GetCpuTiming().TotalMs();
                    const auto &update = scene->GetUpdateTimingStats();
                    const auto &render = engine.GetRhiRenderService().GetTimingStats();
                    const auto &ui = PlutoGE::render::RmlUiRuntime::Get().GetCpuTiming();
                    double scriptsMs = 0, animationMs = 0;
                    for (const auto &timing : update.scriptUpdateTimings) scriptsMs += timing.totalMs;
                    for (const auto &timing : update.componentTimings)
                        if (timing.name.find("AnimationComponent") != std::string::npos) animationMs += timing.totalMs;
                    sample.details = {update.preparationMs, update.runtimeUiMs, update.componentsMs, update.physicsMs,
                        update.lateScriptsMs, update.audioMs, update.renderSubmissionMs, render.commandTranslationMs,
                        render.skinningUploadMs, render.sceneSetupMs, render.renderRecordingMs, render.beginFrameMs,
                        render.shadowRecordingMs, render.geometryRecordingMs, render.postProcessRecordingMs, render.submitMs,
                        scriptsMs, animationMs, render.skinningWaitMs, render.skinningCallerMs,
                        ui.synchronizeMs, ui.inputUpdateMs, ui.renderMs,
                        static_cast<double>(render.skinningVertexCount), static_cast<double>(render.recordedGeometryDrawCount),
                        static_cast<double>(render.recordedShadowDrawCount)};
                    if (useVulkanRenderer)
                    {
                        const auto gpu = engine.GetRenderDevice()->GetTimingStats("Scene");
                        sample.gpuAvailable = gpu.hasGpuResult;
                        sample.gpuMs = gpu.frameGpuMs;
                        sample.skinningMs = engine.GetRhiRenderService().GetTimingStats().skinningDeformationMs;
                        std::map<std::string, float> gpuScopeTotals;
                        for (const auto &scope : gpu.gpuScopes)
                        {
                            if (scope.name == "RHI VSM Planning / Receiver requests") sample.shadowRequestMs = scope.milliseconds;
                            if (gpu.hasGpuResult) gpuScopeTotals[scope.name] += scope.milliseconds;
                        }
                        for (const auto &[name, milliseconds] : gpuScopeTotals)
                            projectBenchmarkGpuScopes[name].push_back(milliseconds);
                    }
                    projectBenchmarkSamples.push_back(sample);
                }
                benchmarkFrameTimes.push_back(
                    std::chrono::duration<double, std::milli>(
                        std::chrono::high_resolution_clock::now() - currentFrameTime).count());
            }
            ++benchmarkFrameIndex;
            if (benchmarkFrameTimes.size() >= benchmarkFrameCount)
                window.RequestClose();
        }

#ifdef _WIN32
        if (!hasLoggedFirstFrame)
        {
            PlutoGE::g_runtimeDiagnostics.Log("First frame completed successfully");
            if (config.temporalUpscaler.technology != PlutoGE::render::rhi::TemporalUpscaler::None)
            {
                const auto &status = engine.GetRhiRenderService().GetTemporalUpscalerStatus();
                PlutoGE::g_runtimeDiagnostics.Log(
                    std::string("Temporal upscaler: ") + (status.active ? "active" : "fallback") +
                    (status.reason.empty() ? std::string{} : " (" + status.reason + ")"));
            }
            hasLoggedFirstFrame = true;
        }
#endif
    }

    runtimeProfiler.reset();
    runtimeUpscaler.Shutdown();
    if (runtimeRenderTarget)
    {
        runtimeRenderTarget->Cleanup();
        runtimeRenderTarget.reset();
    }

    if (benchmarkEnabled && !projectBenchmark && !benchmarkFrameTimes.empty())
    {
        std::sort(benchmarkFrameTimes.begin(), benchmarkFrameTimes.end());
        double totalMilliseconds = 0.0;
        for (const double frameTime : benchmarkFrameTimes)
            totalMilliseconds += frameTime;

        const auto percentile = [&](double fraction)
        {
            const auto index = (std::min)(
                benchmarkFrameTimes.size() - 1,
                static_cast<std::size_t>(fraction * static_cast<double>(benchmarkFrameTimes.size() - 1)));
            return benchmarkFrameTimes[index];
        };
        const double averageMilliseconds = totalMilliseconds / static_cast<double>(benchmarkFrameTimes.size());
        const auto reportPath = executablePath.parent_path() / (executablePath.stem().string() + ".benchmark.txt");
        std::ofstream report(reportPath, std::ios::out | std::ios::trunc);
        if (report.is_open())
        {
            report << "Resolution: " << window.GetExtents().width << " x " << window.GetExtents().height << '\n'
                   << "Frames: " << benchmarkFrameTimes.size() << '\n'
                   << "Average: " << averageMilliseconds << " ms (" << 1000.0 / averageMilliseconds << " FPS)\n"
                   << "Min: " << benchmarkFrameTimes.front() << " ms\n"
                   << "Median: " << percentile(0.50) << " ms\n"
                   << "P95: " << percentile(0.95) << " ms\n"
                   << "P99: " << percentile(0.99) << " ms\n"
                   << "Max: " << benchmarkFrameTimes.back() << " ms\n";
        }
    }

#ifdef _WIN32
    PlutoGE::g_runtimeDiagnostics.currentPhase = "shutdown";
    PlutoGE::g_runtimeDiagnostics.Log("Window requested close");
#endif
    bool benchmarkSucceeded = true;
    if (projectBenchmark)
    {
        benchmarkSucceeded = projectBenchmarkSamples.size() == projectBenchmark->frames;
        try
        {
            if (benchmarkSucceeded)
            {
                PlutoGE::WriteProjectBenchmark(projectBenchmark->output, projectBenchmarkSamples);
                PlutoGE::WriteProjectBenchmarkGpuScopes(projectBenchmark->output, std::move(projectBenchmarkGpuScopes));
            }
            else std::cerr << "Benchmark ended before collecting the requested samples.\n";
        }
        catch (const std::exception &error) { std::cerr << error.what() << '\n'; benchmarkSucceeded = false; }
    }
    if (projectBenchmark) std::cerr << "Benchmark: stopping gameplay\n";
    engine.StopRuntime();
    if (projectBenchmark) std::cerr << "Benchmark: releasing scene\n";
    engine.SetScene(nullptr);
    scene.reset();
    if (projectBenchmark) std::cerr << "Benchmark: shutting down engine\n";
    engine.Shutdown();
    if (projectBenchmark) std::cerr << "Benchmark: shutdown complete\n";

#ifdef _WIN32
    PlutoGE::g_runtimeDiagnostics.Log("Runtime shutdown complete");
#endif
    return benchmarkSucceeded ? 0 : 1;
}

#if defined(_WIN32)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    // GUI executables do not allocate a console. Join the caller's console when
    // launched from a terminal, preserving any inherited file/pipe redirection.
    const auto isRedirected = [](DWORD stream)
    {
        const HANDLE handle = GetStdHandle(stream);
        if (handle == nullptr || handle == INVALID_HANDLE_VALUE) return false;
        const DWORD type = GetFileType(handle);
        return type == FILE_TYPE_DISK || type == FILE_TYPE_PIPE;
    };
    const bool inputRedirected = isRedirected(STD_INPUT_HANDLE);
    const bool outputRedirected = isRedirected(STD_OUTPUT_HANDLE);
    const bool errorRedirected = isRedirected(STD_ERROR_HANDLE);
    if (AttachConsole(ATTACH_PARENT_PROCESS))
    {
        if (!inputRedirected) static_cast<void>(std::freopen("CONIN$", "r", stdin));
        if (!outputRedirected) static_cast<void>(std::freopen("CONOUT$", "w", stdout));
        if (!errorRedirected) static_cast<void>(std::freopen("CONOUT$", "w", stderr));
        std::cin.clear();
        std::cout.clear();
        std::cerr.clear();
        std::clog.clear();
    }
    return RunRuntime(__argc, __argv);
}
#else
int main(int argc, char **argv)
{
    return RunRuntime(argc, argv);
}
#endif
