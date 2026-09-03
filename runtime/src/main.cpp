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
#include <cmath>
#include <cstdlib>
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
            g_runtimeDiagnostics.Log(std::string("Scene environment path exists: ") + (scene->GetEnvironmentMapPath().empty() ? "no" : (std::filesystem::exists(scene->GetEnvironmentMapPath()) ? "yes" : "no")));
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
    if (argc > 1 && std::string_view(argv[1]) == "--export")
    {
        if (argc != 4)
        {
            std::cerr << "Usage: PlutoGERuntime --export <project.plutoproject> <output executable>" << std::endl;
            return 2;
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
                                                       &exportError))
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

    constexpr std::size_t benchmarkWarmupFrames = 120;
    constexpr std::size_t defaultBenchmarkFrames = 600;
    const bool benchmarkEnabled = argc > 1 && std::string_view(argv[1]) == "--benchmark";
    std::size_t benchmarkFrameCount = defaultBenchmarkFrames;
    if (benchmarkEnabled && argc > 2)
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
    auto manifestPath = argc > 1 && !benchmarkEnabled
                            ? std::filesystem::path(argv[1])
                            : PlutoGE::assets::GetRuntimeManifestPathForExecutable(executablePath);

    if (argc <= 1 || benchmarkEnabled)
    {
        const auto contentPackPath = PlutoGE::assets::GetRuntimeContentPackPathForExecutable(executablePath);
        if (std::filesystem::exists(contentPackPath))
        {
            std::error_code temporaryError;
            const auto temporaryRoot = std::filesystem::temp_directory_path(temporaryError);
            const auto uniqueName = executablePath.stem().string() + "-" +
                                    std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
            temporaryContent.path = temporaryRoot / "PlutoGE" / "Content" / uniqueName;
            std::string unpackError;
            if (temporaryError || !PlutoGE::assets::ExtractStandaloneProjectContent(contentPackPath, temporaryContent.path, &unpackError))
            {
                std::cerr << (unpackError.empty() ? "Failed to mount the game content pack." : unpackError) << std::endl;
                return 1;
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
            .visible = true,
            .fullscreen = false,
        }};
    config.vSync = project->GetManifest().vSyncEnabled;
    config.graphicsApi = project->GetManifest().graphicsApi;
    if (config.graphicsApi == PlutoGE::render::rhi::GraphicsApi::Vulkan &&
        project->GetManifest().runtimeUpscaler == PlutoGE::assets::RuntimeUpscalerMode::Dlss)
    {
        config.temporalUpscaler.technology = PlutoGE::render::rhi::TemporalUpscaler::Dlss;
        config.temporalUpscaler.quality = project->GetManifest().runtimeDlssQuality;
        config.temporalUpscaler.hdr = true;
        config.temporalUpscaler.autoExposure = true;
    }

    if (!engine.Initialize(config))
    {
#ifdef _WIN32
        PlutoGE::g_runtimeDiagnostics.Log("Engine initialization failed");
#endif
        std::cerr << "Failed to initialize runtime engine." << std::endl;
        return 1;
    }

    if (config.graphicsApi == PlutoGE::render::rhi::GraphicsApi::OpenGL)
        engine.GetRenderer().SetVSyncEnabled(project->GetManifest().vSyncEnabled);

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

    auto scene = PlutoGE::scene::SceneSerializer::Load(
        startupScenePath,
        &errorMessage,
#ifdef _WIN32
        [](std::string_view message)
        {
            PlutoGE::g_runtimeDiagnostics.Log(std::string(message));
        }
#else
        {}
#endif
    );
    if (!scene)
    {
#ifdef _WIN32
        PlutoGE::g_runtimeDiagnostics.Log("Failed to load startup scene: " + errorMessage);
#endif
        std::cerr << (errorMessage.empty() ? "Failed to load startup scene." : errorMessage) << std::endl;
        engine.Shutdown();
        return 1;
    }

    engine.SetScene(scene.get());
    engine.StartRuntime();

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
    if (benchmarkEnabled)
        benchmarkFrameTimes.reserve(benchmarkFrameCount);

    while (!window.ShouldClose())
    {
        const auto currentFrameTime = std::chrono::high_resolution_clock::now();
        const float deltaTime = std::chrono::duration<float>(currentFrameTime - lastFrameTime).count();
        lastFrameTime = currentFrameTime;

#ifdef _WIN32
        PlutoGE::g_runtimeDiagnostics.currentPhase = "scene update";
#endif
        if (scene)
        {
            scene->Update(deltaTime);
        }

        if (const auto requestedScene = engine.ConsumeSceneLoadRequest())
        {
            const std::string reference = project->FindSceneAssetReference(*requestedScene);
            const std::string requestedPath = reference.empty() ? std::string{} : engine.GetAssetManager().ResolveAssetPath(reference);
            std::string sceneLoadError;
            auto nextScene = requestedPath.empty() ? nullptr : PlutoGE::scene::SceneSerializer::Load(requestedPath, &sceneLoadError);
            if (nextScene)
            {
                engine.SetScene(nextScene.get());
                scene = std::move(nextScene);
#ifdef _WIN32
                PlutoGE::g_runtimeDiagnostics.Log("Loaded scene from script: " + requestedPath);
#endif
            }
            else
            {
                const std::string detail = sceneLoadError.empty() ? "scene asset was not found" : sceneLoadError;
                std::cerr << "Failed to load scene '" << *requestedScene << "': " << detail << std::endl;
#ifdef _WIN32
                PlutoGE::g_runtimeDiagnostics.Log("Failed scripted scene load '" + *requestedScene + "': " + detail);
#endif
            }
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
                PlutoGE::render::BasicLighting lighting;
                lighting.cameraPosition = glm::vec3(glm::inverse(cameraData.view)[3]);
                for (const auto *light : scene->GetLights())
                    if (light && light->type == PlutoGE::scene::LightType::Directional)
                    {
                        lighting.directionalDirection = light->direction;
                        lighting.directionalColor = light->color;
                        lighting.directionalIntensity = light->intensity;
                        lighting.shadowsEnabled = light->castsShadows;
                        break;
                    }
                const auto readTexturePixels = [](const PlutoGE::render::Texture &texture)
                {
                    const auto source = texture.GetRgba8Pixels();
                    return std::vector<std::byte>(reinterpret_cast<const std::byte *>(source.data()),
                                                  reinterpret_cast<const std::byte *>(source.data() + source.size()));
                };
                if (!engine.GetRhiRenderService().RenderSceneAndPresent(
                        cameraData, lighting, renderer.GetSceneRenderCommands(), readTexturePixels, scene.get()))
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

#ifdef _WIN32
        PlutoGE::g_runtimeDiagnostics.currentPhase = "poll events";
#endif
        window.PollEvents();

        if (benchmarkEnabled)
        {
            if (benchmarkFrameIndex >= benchmarkWarmupFrames)
            {
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
            hasLoggedFirstFrame = true;
        }
#endif
    }

    runtimeUpscaler.Shutdown();
    if (runtimeRenderTarget)
    {
        runtimeRenderTarget->Cleanup();
        runtimeRenderTarget.reset();
    }

    if (benchmarkEnabled && !benchmarkFrameTimes.empty())
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
    engine.StopRuntime();
    engine.SetScene(nullptr);
    scene.reset();
    engine.Shutdown();

#ifdef _WIN32
    PlutoGE::g_runtimeDiagnostics.Log("Runtime shutdown complete");
#endif
    return 0;
}

#if defined(_WIN32) && defined(PLUTO_RUNTIME_WINDOWED)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    return RunRuntime(__argc, __argv);
}
#else
int main(int argc, char **argv)
{
    return RunRuntime(argc, argv);
}
#endif
