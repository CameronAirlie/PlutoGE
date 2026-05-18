#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace PlutoGE
{
    namespace
    {
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

int main(int argc, char **argv)
{
    const auto executablePath = PlutoGE::ResolveExecutablePath(argv);
    const auto manifestPath = argc > 1
                                  ? std::filesystem::path(argv[1])
                                  : PlutoGE::assets::GetRuntimeManifestPathForExecutable(executablePath);

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

    if (!engine.Initialize(config))
    {
#ifdef _WIN32
        PlutoGE::g_runtimeDiagnostics.Log("Engine initialization failed");
#endif
        std::cerr << "Failed to initialize runtime engine." << std::endl;
        return 1;
    }

    engine.GetRenderer().SetVSyncEnabled(project->GetManifest().vSyncEnabled);

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

    auto scene = PlutoGE::scene::SceneSerializer::Load(startupScenePath, &errorMessage);
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
    bool hasLoggedFirstFrame = false;
    bool hasLoggedFirstFrameDiagnostics = false;

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
        renderer.BeginFrame();
        if (auto *cameraComponent = PlutoGE::FindFirstSceneCamera(scene.get()))
        {
#ifdef _WIN32
            PlutoGE::g_runtimeDiagnostics.currentPhase = "render frame";
#endif
            renderer.RenderFrame(*cameraComponent, nullptr, scene->GetLights());
        }
        renderer.ClearRenderCommands();

#ifdef _WIN32
        PlutoGE::g_runtimeDiagnostics.currentPhase = "end frame";
#endif
        renderer.EndFrame();

#ifdef _WIN32
        PlutoGE::g_runtimeDiagnostics.currentPhase = "poll events";
#endif
        window.PollEvents();

#ifdef _WIN32
        if (!hasLoggedFirstFrame)
        {
            PlutoGE::g_runtimeDiagnostics.Log("First frame completed successfully");
            hasLoggedFirstFrame = true;
        }
#endif
    }

#ifdef _WIN32
    PlutoGE::g_runtimeDiagnostics.currentPhase = "shutdown";
    PlutoGE::g_runtimeDiagnostics.Log("Window requested close");
#endif
    engine.SetScene(nullptr);
    scene.reset();
    engine.Shutdown();

#ifdef _WIN32
    PlutoGE::g_runtimeDiagnostics.Log("Runtime shutdown complete");
#endif
    return 0;
}