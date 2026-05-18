#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/ui/panels/ProfilerPanel.h"
#include "PlutoGE/ui/panels/ViewportPanel.h"
#include "PlutoGE/ui/panels/SceneHierarchyPanel.h"
#include "PlutoGE/ui/panels/InspectorPanel.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scripting/ScriptEngine.h"
#include "PlutoGE/scene/SceneBaker.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <iostream>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

namespace PlutoGE::ui
{
    namespace
    {
        constexpr float kEditorCameraMoveSpeed = 6.0f;
        constexpr float kEditorCameraBoostMultiplier = 2.5f;
        constexpr float kEditorCameraMouseSensitivity = 0.12f;
        constexpr float kEditorCameraPitchLimitDegrees = 89.0f;
        constexpr size_t kProjectSettingsPathBufferSize = 512;
        constexpr const char *kSceneFileFilter = "PlutoGE Scene\0*.plutoscene\0All Files\0*.*\0";
        constexpr const char *kProjectFileFilter = "PlutoGE Project\0*.plutoproject\0All Files\0*.*\0";
        constexpr const char *kExecutableFileFilter = "Windows Executable\0*.exe\0All Files\0*.*\0";

        bool IsCameraActiveInScene(scene::Scene *scene, scene::CameraComponent *cameraComponent)
        {
            if (!scene || !cameraComponent)
            {
                return false;
            }

            auto *owner = cameraComponent->GetOwner();
            if (!owner || !cameraComponent->GetCamera() || !owner->IsActive())
            {
                return false;
            }

            return scene->FindEntityByID(owner->GetID()) == owner;
        }

        glm::mat4 GetEditorCameraTransform(const EditorShell::EditorViewportCamera &camera)
        {
            glm::mat4 transform = glm::translate(glm::mat4(1.0f), camera.position);
            transform = glm::rotate(transform, glm::radians(camera.yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
            transform = glm::rotate(transform, glm::radians(camera.pitchDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
            return transform;
        }

        glm::vec3 GetTransformForward(const glm::mat4 &transform)
        {
            return glm::normalize(-glm::vec3(transform[2]));
        }

        glm::vec3 GetTransformRight(const glm::mat4 &transform)
        {
            return glm::normalize(glm::vec3(transform[0]));
        }

        void SetCursorCapture(GLFWwindow *windowHandle, bool captured)
        {
            if (!windowHandle)
            {
                return;
            }

            glfwSetInputMode(windowHandle, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        }

        void UpdateEditorCamera(EditorShell::EditorViewportCamera &camera,
                                GLFWwindow *windowHandle,
                                bool canActivate,
                                float deltaTime,
                                bool &isLookActive,
                                double &lastCursorX,
                                double &lastCursorY)
        {
            if (!windowHandle)
            {
                if (isLookActive)
                {
                    SetCursorCapture(windowHandle, false);
                    isLookActive = false;
                }
                return;
            }

            const bool isRightMouseDown = glfwGetMouseButton(windowHandle, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
            if (!isRightMouseDown)
            {
                if (isLookActive)
                {
                    SetCursorCapture(windowHandle, false);
                    isLookActive = false;
                }
                return;
            }

            if (!isLookActive)
            {
                if (!canActivate)
                {
                    return;
                }

                isLookActive = true;
                SetCursorCapture(windowHandle, true);
                glfwGetCursorPos(windowHandle, &lastCursorX, &lastCursorY);
            }

            double cursorX = 0.0;
            double cursorY = 0.0;
            glfwGetCursorPos(windowHandle, &cursorX, &cursorY);

            const float deltaX = static_cast<float>(cursorX - lastCursorX);
            const float deltaY = static_cast<float>(cursorY - lastCursorY);
            lastCursorX = cursorX;
            lastCursorY = cursorY;

            camera.yawDegrees -= deltaX * kEditorCameraMouseSensitivity;
            camera.pitchDegrees = glm::clamp(camera.pitchDegrees - deltaY * kEditorCameraMouseSensitivity,
                                             -kEditorCameraPitchLimitDegrees,
                                             kEditorCameraPitchLimitDegrees);

            const glm::mat4 transform = GetEditorCameraTransform(camera);

            glm::vec3 movement(0.0f);
            const glm::vec3 forward = GetTransformForward(transform);
            const glm::vec3 right = GetTransformRight(transform);
            static constexpr glm::vec3 kWorldUp(0.0f, 1.0f, 0.0f);

            if (glfwGetKey(windowHandle, GLFW_KEY_W) == GLFW_PRESS)
            {
                movement += forward;
            }
            if (glfwGetKey(windowHandle, GLFW_KEY_S) == GLFW_PRESS)
            {
                movement -= forward;
            }
            if (glfwGetKey(windowHandle, GLFW_KEY_D) == GLFW_PRESS)
            {
                movement += right;
            }
            if (glfwGetKey(windowHandle, GLFW_KEY_A) == GLFW_PRESS)
            {
                movement -= right;
            }
            if (glfwGetKey(windowHandle, GLFW_KEY_E) == GLFW_PRESS)
            {
                movement += kWorldUp;
            }
            if (glfwGetKey(windowHandle, GLFW_KEY_Q) == GLFW_PRESS)
            {
                movement -= kWorldUp;
            }

            if (glm::dot(movement, movement) <= 0.0f)
            {
                return;
            }

            float moveSpeed = kEditorCameraMoveSpeed;
            if (glfwGetKey(windowHandle, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
            {
                moveSpeed *= kEditorCameraBoostMultiplier;
            }

            camera.position += glm::normalize(movement) * moveSpeed * deltaTime;
        }

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

        std::unique_ptr<scene::Scene> CreateEmptyScene()
        {
            return std::make_unique<scene::Scene>();
        }

#ifdef _WIN32
        std::string ShowOpenFileDialog(const char *filter)
        {
            OPENFILENAMEA openFileName{};
            char fileName[MAX_PATH] = "";
            openFileName.lStructSize = sizeof(openFileName);
            openFileName.hwndOwner = nullptr;
            openFileName.lpstrFilter = filter;
            openFileName.lpstrFile = fileName;
            openFileName.nMaxFile = MAX_PATH;
            openFileName.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (!GetOpenFileNameA(&openFileName))
            {
                return {};
            }

            return std::filesystem::path(fileName).lexically_normal().string();
        }

        std::string ShowSaveFileDialog(const char *filter, const std::string &initialPath, const char *defaultExtension)
        {
            OPENFILENAMEA openFileName{};
            char fileName[MAX_PATH] = "";
            if (!initialPath.empty())
            {
                strncpy_s(fileName, sizeof(fileName), initialPath.c_str(), _TRUNCATE);
            }
            openFileName.lStructSize = sizeof(openFileName);
            openFileName.hwndOwner = nullptr;
            openFileName.lpstrFilter = filter;
            openFileName.lpstrFile = fileName;
            openFileName.nMaxFile = MAX_PATH;
            openFileName.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
            openFileName.lpstrDefExt = defaultExtension;
            if (!GetSaveFileNameA(&openFileName))
            {
                return {};
            }

            return std::filesystem::path(fileName).lexically_normal().string();
        }
#else
        std::string ShowOpenFileDialog(const char *)
        {
            return {};
        }

        std::string ShowSaveFileDialog(const char *, const std::string &, const char *)
        {
            return {};
        }
#endif

        bool IsMeshAssetProperty(std::string_view propertyName)
        {
            return propertyName == "SourceMesh" || propertyName.ends_with("LightmapPath");
        }

        bool ValidateSceneForProjectExport(const scene::Scene &scene, core::Engine &engine, std::string *errorMessage)
        {
            auto validatePath = [&](std::string_view label, const std::string &path) -> bool
            {
                if (path.empty())
                {
                    return true;
                }

                const std::string persistedPath = engine.GetAssetManager().PersistAssetPath(path);
                if (assets::Project::IsProjectAssetReference(persistedPath) || assets::Project::IsEngineAssetReference(persistedPath))
                {
                    return true;
                }

                if (errorMessage)
                {
                    *errorMessage = std::string(label) + " must be stored inside the project Assets folder or be an engine asset.";
                }
                return false;
            };

            if (!validatePath("Environment map", scene.GetEnvironmentMapPath()))
            {
                return false;
            }

            std::vector<scene::Entity *> entities;
            bool hasMainCamera = false;
            for (auto *rootEntity : scene.GetRootEntities())
            {
                CollectEntitiesRecursive(rootEntity, entities);
            }

            for (auto *entity : entities)
            {
                if (!entity)
                {
                    continue;
                }

                if (auto *meshComponent = entity->GetComponent<scene::MeshComponent>())
                {
                    for (const auto &property : meshComponent->Serialize())
                    {
                        if (IsMeshAssetProperty(property.name) && !validatePath(property.name, property.value))
                        {
                            return false;
                        }
                    }
                }

                if (auto *cameraComponent = entity->GetComponent<scene::CameraComponent>())
                {
                    if (entity->IsActive() && cameraComponent->IsEnabled() && cameraComponent->GetCamera() && cameraComponent->IsMainCamera())
                    {
                        hasMainCamera = true;
                    }
                }
            }

            if (!hasMainCamera)
            {
                if (errorMessage)
                {
                    *errorMessage = "Project main scene must contain an active Main Camera.";
                }
                return false;
            }

            return true;
        }

#ifdef _WIN32
        std::filesystem::path GetProcessExecutablePath()
        {
            char buffer[MAX_PATH] = "";
            const DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
            if (length == 0)
            {
                return {};
            }

            return std::filesystem::path(buffer).lexically_normal();
        }
#else
        std::filesystem::path GetProcessExecutablePath()
        {
            return {};
        }
#endif

        struct RuntimeBuildInfo
        {
            std::filesystem::path buildDirectory;
            std::filesystem::path runtimeExecutablePath;
            std::string configuration;
        };

        std::string QuoteShellArgument(const std::string &value)
        {
            return '"' + value + '"';
        }

        bool TryGetRuntimeBuildInfo(const std::filesystem::path &editorExecutablePath, RuntimeBuildInfo &buildInfo)
        {
            if (editorExecutablePath.empty())
            {
                return false;
            }

            const auto editorExecutableDirectory = editorExecutablePath.parent_path();
            const auto runtimeFileName = std::string("PlutoGERuntime") + editorExecutablePath.extension().string();
            if (editorExecutableDirectory.empty())
            {
                return false;
            }

            if (editorExecutableDirectory.filename() == "editor")
            {
                buildInfo.buildDirectory = editorExecutableDirectory.parent_path().lexically_normal();
                buildInfo.runtimeExecutablePath = (buildInfo.buildDirectory / "runtime" / runtimeFileName).lexically_normal();
                buildInfo.configuration.clear();
                return true;
            }

            if (editorExecutableDirectory.has_parent_path() && editorExecutableDirectory.parent_path().filename() == "editor")
            {
                buildInfo.buildDirectory = editorExecutableDirectory.parent_path().parent_path().lexically_normal();
                buildInfo.configuration = editorExecutableDirectory.filename().string();
                buildInfo.runtimeExecutablePath = (buildInfo.buildDirectory / "runtime" / buildInfo.configuration / runtimeFileName).lexically_normal();
                return true;
            }

            return false;
        }

        bool BuildRuntimeExecutable(const RuntimeBuildInfo &buildInfo, std::string *errorMessage)
        {
            if (buildInfo.buildDirectory.empty())
            {
                if (errorMessage)
                {
                    *errorMessage = "Unable to determine the active CMake build directory for PlutoGERuntime.";
                }
                return false;
            }

            std::string buildCommand = "cmake --build " + QuoteShellArgument(buildInfo.buildDirectory.string()) + " --target PlutoGERuntime";
            if (!buildInfo.configuration.empty())
            {
                buildCommand += " --config " + QuoteShellArgument(buildInfo.configuration);
            }

            if (std::system(buildCommand.c_str()) != 0)
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to build PlutoGERuntime in the current editor configuration.";
                }
                return false;
            }

            if (!buildInfo.runtimeExecutablePath.empty() && std::filesystem::exists(buildInfo.runtimeExecutablePath))
            {
                return true;
            }

            if (errorMessage)
            {
                *errorMessage = "PlutoGERuntime built, but the expected executable was not found at: " + buildInfo.runtimeExecutablePath.string();
            }
            return false;
        }

        std::filesystem::path FindBuiltRuntimeExecutable()
        {
            const auto executablePath = GetProcessExecutablePath();
            RuntimeBuildInfo runtimeBuildInfo;
            if (TryGetRuntimeBuildInfo(executablePath, runtimeBuildInfo) && !runtimeBuildInfo.runtimeExecutablePath.empty() && std::filesystem::exists(runtimeBuildInfo.runtimeExecutablePath))
            {
                return runtimeBuildInfo.runtimeExecutablePath;
            }

            std::vector<std::filesystem::path> searchRoots;
            if (!executablePath.empty())
            {
                searchRoots.push_back(executablePath.parent_path());
                if (executablePath.parent_path().has_parent_path())
                {
                    searchRoots.push_back(executablePath.parent_path().parent_path());
                }
                if (executablePath.parent_path().has_parent_path() && executablePath.parent_path().parent_path().has_parent_path())
                {
                    searchRoots.push_back(executablePath.parent_path().parent_path().parent_path());
                }
            }
            searchRoots.push_back(std::filesystem::current_path());

            for (const auto &searchRoot : searchRoots)
            {
                if (searchRoot.empty() || !std::filesystem::exists(searchRoot))
                {
                    continue;
                }

                const auto runtimeExecutablePath = assets::FindRuntimeExecutable(searchRoot);
                if (!runtimeExecutablePath.empty())
                {
                    return runtimeExecutablePath;
                }
            }

            return {};
        }
    }

    EditorShell::~EditorShell() = default;

    void EditorShell::Initialize()
    {
        auto config = core::EngineConfig{
            platform::WindowConfig{
                .title = "PlutoGE Editor",
                .width = 1280,
                .height = 720,
                .resizable = true,
                .visible = true,
                .fullscreen = false,
            }};
        if (!m_engine.Initialize(config))
        {
            std::cerr << "Failed to initialize Engine in EditorShell" << std::endl;
        }

        m_editorCamera = EditorViewportCamera{};
        m_editorCamera.AddPostProcessEffectByType("RSM");
        m_editorCamera.AddPostProcessEffectByType("VolumetricFog");
        m_editorCamera.AddPostProcessEffectByType("LSAO");
        m_editorCamera.AddPostProcessEffectByType("ToneMapping");
        m_editorCamera.AddPostProcessEffectByType("SceneComposite");
        m_editorCamera.AddPostProcessEffectByType("GammaCorrection");

        m_scene = CreateEmptyScene();
        m_engine.SetScene(m_scene.get());
        m_engine.GetAssetManager().ClearProjectContext();
        m_statusMessage = "Ready";

        m_panelManager.InitializeImGui(&m_engine.GetWindow());
    }

    glm::vec3 randomColour()
    {
        // maximum value for each color channel is 255, so we divide by 255 to get a value between 0 and 1
        return glm::vec3(
            static_cast<float>(rand()) / static_cast<float>(RAND_MAX),
            static_cast<float>(rand()) / static_cast<float>(RAND_MAX),
            static_cast<float>(rand()) / static_cast<float>(RAND_MAX));
    }

    void EditorShell::Render()
    {
        auto &window = m_engine.GetWindow();
        auto &renderer = m_engine.GetRenderer();
        auto deltaTime = std::chrono::duration<float>::zero();
        auto lastTime = std::chrono::high_resolution_clock::now();

        ViewportPanelConfig viewportConfig;
        viewportConfig.name = "Editor Viewport";
        viewportConfig.clearColor = glm::vec4(0.1f, 0.1f, 0.15f, 1.0f);
        viewportConfig.initialRenderScale = 1.0f;
        auto *viewportPanel = new ViewportPanel(viewportConfig);
        viewportPanel->Initialize();

        auto *sceneHierarchyPanel = new SceneHierarchyPanel(PanelConfig{"Scene Hierarchy"});
        sceneHierarchyPanel->Initialize();

        m_panelManager.AddPanel(viewportPanel);
        m_panelManager.AddPanel(sceneHierarchyPanel);

        auto *renderTarget = viewportPanel->GetRenderTarget();

        ViewportPanelConfig viewportConfig2;
        viewportConfig2.name = "Game Viewport";
        viewportConfig2.openByDefault = true;
        viewportConfig2.clearColor = glm::vec4(0.15f, 0.1f, 0.1f, 1.0f);
        viewportConfig2.initialRenderScale = 1.0f;
        auto viewportPanel2 = new ViewportPanel(viewportConfig2);
        viewportPanel2->Initialize();
        m_panelManager.AddPanel(viewportPanel2);

        auto inspectorPanel = new InspectorPanel(PanelConfig{"Inspector"});
        inspectorPanel->Initialize();
        m_panelManager.AddPanel(inspectorPanel);

        auto profilerPanel = new ProfilerPanel(PanelConfig{"Profiler"}, &m_profiler, &m_panelManager, &renderer);
        profilerPanel->Initialize();
        m_panelManager.AddPanel(profilerPanel);

        auto *renderTarget2 = viewportPanel2->GetRenderTarget();
        auto *windowHandle = static_cast<GLFWwindow *>(window.GetWindow());
        bool isEditorCameraLookActive = false;
        double lastEditorCameraCursorX = 0.0;
        double lastEditorCameraCursorY = 0.0;

        renderer.SetVSyncEnabled(true);

        while (!window.ShouldClose())
        {
            auto currentTime = std::chrono::high_resolution_clock::now();
            deltaTime = currentTime - lastTime;
            const float deltaSeconds = deltaTime.count();
            EditorFrameTimingStats frameTimingStats{};
            renderer.BeginProfilingFrame();

            const auto renderTargetWidth = renderTarget->GetWidth();
            const auto renderTargetHeight = renderTarget->GetHeight();
            const auto renderTarget2Width = renderTarget2->GetWidth();
            const auto renderTarget2Height = renderTarget2->GetHeight();

            const bool bakeTaskFinished = m_activeBakeTask && m_activeBakeTask->IsFinished();
            if (bakeTaskFinished && m_scene)
            {
                const auto bakeResult = m_activeBakeTask->Finalize(*m_scene);
                m_statusMessage = bakeResult.message;
                std::cout << bakeResult.message << std::endl;

                if (bakeResult.succeeded && !m_scene->GetFilePath().empty())
                {
                    std::string errorMessage;
                    if (scene::SceneSerializer::Save(*m_scene, m_scene->GetFilePath(), &errorMessage))
                    {
                        m_statusMessage += " Saved scene.";
                    }
                    else if (!errorMessage.empty())
                    {
                        m_statusMessage += " " + errorMessage;
                    }
                }

                m_activeBakeTask.reset();
            }

            const bool isBakeRunning = m_activeBakeTask && m_activeBakeTask->IsRunning();
            if (isBakeRunning)
            {
                m_statusMessage = m_activeBakeTask->GetStatusMessage();
            }

            // Scene update

            const auto sceneUpdateStart = std::chrono::high_resolution_clock::now();
            if (!isBakeRunning)
            {
                m_engine.UpdateAsyncMeshImports();
                if (m_scene)
                {
                    m_scene->Update(deltaTime.count());
                }
            }
            const auto sceneUpdateEnd = std::chrono::high_resolution_clock::now();
            frameTimingStats.sceneUpdateMs = std::chrono::duration<float, std::milli>(sceneUpdateEnd - sceneUpdateStart).count();

            UpdateEditorCamera(m_editorCamera,
                               windowHandle,
                               viewportPanel->IsViewportHovered() || viewportPanel->IsViewportFocused(),
                               deltaSeconds,
                               isEditorCameraLookActive,
                               lastEditorCameraCursorX,
                               lastEditorCameraCursorY);

            auto *cameraComponent2 = FindFirstSceneCamera(m_scene.get());
            const bool shouldRenderViewport1 = viewportPanel->ShouldRenderFrame();
            const bool shouldRenderViewport2 = viewportPanel2->ShouldRenderFrame() && IsCameraActiveInScene(m_scene.get(), cameraComponent2);

            const auto viewportRenderStart = std::chrono::high_resolution_clock::now();
            if (shouldRenderViewport1)
            {
                ++frameTimingStats.renderedViewportCount;
                const glm::mat4 editorCameraTransform = GetEditorCameraTransform(m_editorCamera);
                const auto editorCameraData = m_editorCamera.camera.GetCameraDataForTransform(editorCameraTransform,
                                                                                              renderTarget->GetWidth(),
                                                                                              renderTarget->GetHeight());
                std::vector<render::IPostProcessEffect *> editorPostProcessEffects;
                editorPostProcessEffects.reserve(m_editorCamera.GetPostProcessEffects().size());
                for (const auto &effect : m_editorCamera.GetPostProcessEffects())
                {
                    editorPostProcessEffects.push_back(effect.get());
                }

                renderer.RenderFrame(editorCameraData, renderTarget, m_scene ? m_scene->GetLights() : std::vector<scene::Light *>{}, &editorPostProcessEffects, m_scene.get());
            }
            else
            {
                viewportPanel->ClearFrame();
            }

            if (shouldRenderViewport2)
            {
                ++frameTimingStats.renderedViewportCount;
                viewportPanel2->RenderFrame(*cameraComponent2);
            }
            else
            {
                viewportPanel2->ClearFrame();
            }
            const auto viewportRenderEnd = std::chrono::high_resolution_clock::now();
            frameTimingStats.viewportRenderMs = std::chrono::duration<float, std::milli>(viewportRenderEnd - viewportRenderStart).count();

            renderer.ClearRenderCommands();

            // UI

            const auto beginFrameStart = std::chrono::high_resolution_clock::now();
            renderer.BeginFrame();
            const auto beginFrameEnd = std::chrono::high_resolution_clock::now();
            frameTimingStats.rendererBeginFrameMs = std::chrono::duration<float, std::milli>(beginFrameEnd - beginFrameStart).count();

            m_panelManager.BeginPanelUpdate();

            if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::GetIO().WantTextInput)
            {
                SetSelectedEntity(nullptr);
            }

            auto sanitizeBakeSettings = [](scene::SceneBakeSettings &settings)
            {
                settings.lightmapResolution = (std::max)(settings.lightmapResolution, 4);
                settings.lightmapTileSize = (std::max)(settings.lightmapTileSize, 1);
                settings.indirectBounceSampleCount = (std::max)(settings.indirectBounceSampleCount, 0);
                settings.probeDirectionCount = (std::max)(settings.probeDirectionCount, 0);
                settings.lightmapBounceStrength = (std::max)(settings.lightmapBounceStrength, 0.0f);
                settings.probeBounceStrength = (std::max)(settings.probeBounceStrength, 0.0f);
            };

            auto resetSelectionState = [&]()
            {
                m_selectedEntity = nullptr;
                m_isEditorCameraSelected = false;
            };

            auto applyProjectContext = [&]()
            {
                if (m_project)
                {
                    m_engine.GetAssetManager().SetProjectContext(m_project->GetRootDirectory().string(), m_project->GetManifest().assetDirectory);
                }
                else
                {
                    m_engine.GetAssetManager().ClearProjectContext();
                }
            };

            auto getDefaultProjectScenePath = [&]() -> std::string
            {
                if (!m_project)
                {
                    return "scene.plutoscene";
                }

                return (m_project->GetAssetDirectoryPath() / "Scenes" / "Main.plutoscene").string();
            };

            auto getConfiguredProjectMainScenePath = [&]() -> std::string
            {
                if (!m_project || m_project->GetManifest().startupScene.empty())
                {
                    return {};
                }

                const auto resolvedPath = m_project->ResolveAssetReference(m_project->GetManifest().startupScene);
                if (!resolvedPath.empty())
                {
                    return resolvedPath.string();
                }

                return m_project->GetManifest().startupScene;
            };

            auto makeProjectMainSceneReference = [&](const std::string &sceneSelection, std::string *errorMessage) -> std::string
            {
                if (!m_project)
                {
                    if (errorMessage)
                    {
                        *errorMessage = "No active project";
                    }
                    return {};
                }

                if (sceneSelection.empty())
                {
                    if (errorMessage)
                    {
                        *errorMessage = "Choose a main scene before saving project settings.";
                    }
                    return {};
                }

                std::string sceneReference = sceneSelection;
                if (!assets::Project::IsProjectAssetReference(sceneReference))
                {
                    sceneReference = m_project->MakeAssetReference(sceneSelection);
                }

                if (!assets::Project::IsProjectAssetReference(sceneReference))
                {
                    if (errorMessage)
                    {
                        *errorMessage = "Main scene must be saved inside the project Assets folder.";
                    }
                    return {};
                }

                const auto resolvedScenePath = m_project->ResolveAssetReference(sceneReference);
                if (resolvedScenePath.empty() || !std::filesystem::exists(resolvedScenePath))
                {
                    if (errorMessage)
                    {
                        *errorMessage = "Main scene file was not found.";
                    }
                    return {};
                }

                if (resolvedScenePath.extension() != ".plutoscene")
                {
                    if (errorMessage)
                    {
                        *errorMessage = "Main scene must be a .plutoscene file.";
                    }
                    return {};
                }

                return sceneReference;
            };

            auto saveCurrentProject = [&](const char *successPrefix)
            {
                if (!m_project)
                {
                    m_statusMessage = "No active project";
                    return false;
                }

                m_project->RefreshAssetRegistry();
                std::string errorMessage;
                if (!m_project->Save(&errorMessage))
                {
                    m_statusMessage = errorMessage.empty() ? "Failed to save project" : errorMessage;
                    return false;
                }

                m_statusMessage = std::string(successPrefix) + m_project->GetManifestPath().filename().string();
                return true;
            };

            auto saveSceneToPath = [&](const std::string &savePath) -> bool
            {
                if (!m_scene || savePath.empty())
                {
                    return false;
                }

                std::string errorMessage;
                if (!scene::SceneSerializer::Save(*m_scene, savePath, &errorMessage))
                {
                    m_statusMessage = errorMessage.empty() ? "Failed to save scene" : errorMessage;
                    return false;
                }

                m_scene->SetFilePath(savePath);

                m_statusMessage = "Saved scene: " + std::filesystem::path(savePath).filename().string();
                return true;
            };

            auto loadSceneIntoEditor = [&](std::unique_ptr<scene::Scene> loadedScene, const std::string &statusText)
            {
                m_scene = std::move(loadedScene);
                m_engine.SetScene(m_scene.get());
                resetSelectionState();
                m_statusMessage = statusText;
            };

            auto openProjectFromPath = [&](const std::string &projectPath)
            {
                std::string errorMessage;
                auto loadedProject = assets::Project::Load(projectPath, &errorMessage);
                if (!loadedProject)
                {
                    m_statusMessage = errorMessage.empty() ? "Failed to open project" : errorMessage;
                    return;
                }

                m_project = std::move(loadedProject);
                applyProjectContext();

                const std::string startupScenePath = m_engine.GetAssetManager().ResolveAssetPath(m_project->GetManifest().startupScene);
                if (startupScenePath.empty())
                {
                    loadSceneIntoEditor(CreateEmptyScene(), "Opened project: " + m_project->GetManifest().name);
                    return;
                }

                auto loadedScene = scene::SceneSerializer::Load(startupScenePath, &errorMessage);
                if (!loadedScene)
                {
                    loadSceneIntoEditor(CreateEmptyScene(), errorMessage.empty() ? "Opened project without startup scene" : errorMessage);
                    return;
                }

                loadSceneIntoEditor(std::move(loadedScene), "Opened project: " + m_project->GetManifest().name);
            };

            auto runBake = [&](const scene::SceneBakeSettings &requestedSettings)
            {
                if (!m_scene || m_activeBakeTask)
                {
                    return;
                }

                scene::SceneBakeSettings bakeSettings = requestedSettings;
                sanitizeBakeSettings(bakeSettings);

                scene::SceneBaker baker;
                scene::SceneBakeResult immediateResult;
                auto bakeTask = baker.BeginBake(*m_scene, bakeSettings, &immediateResult);
                if (!bakeTask)
                {
                    m_statusMessage = immediateResult.message;
                    std::cout << immediateResult.message << std::endl;
                    return;
                }

                m_activeBakeTask = std::move(bakeTask);
                m_statusMessage = m_activeBakeTask->GetStatusMessage();
                std::cout << m_statusMessage << std::endl;
            };

            static std::array<char, kProjectSettingsPathBufferSize> projectMainSceneBuffer{};
            auto openProjectSettingsPopup = [&]()
            {
                std::fill(projectMainSceneBuffer.begin(), projectMainSceneBuffer.end(), '\0');
                const std::string configuredScenePath = getConfiguredProjectMainScenePath();
                strncpy_s(projectMainSceneBuffer.data(), projectMainSceneBuffer.size(), configuredScenePath.c_str(), _TRUNCATE);
                ImGui::OpenPopup("Project Settings");
            };
            bool projectSettingsPopupRequested = false;

            // Toolbar menu
            if (ImGui::BeginMainMenuBar())
            {
                if (ImGui::BeginMenu("File"))
                {
                    if (isBakeRunning && ImGui::MenuItem("Cancel Bake"))
                    {
                        m_activeBakeTask->Cancel();
                        m_statusMessage = "Cancelling bake...";
                    }
                    if (isBakeRunning)
                    {
                        ImGui::Separator();
                    }

                    ImGui::BeginDisabled(isBakeRunning);
                    if (ImGui::MenuItem("New Project..."))
                    {
                        const std::string projectPath = ShowSaveFileDialog(kProjectFileFilter, "PlutoProject.plutoproject", "plutoproject");
                        if (!projectPath.empty())
                        {
                            std::string errorMessage;
                            auto createdProject = assets::Project::Create(projectPath, std::filesystem::path(projectPath).stem().string(), &errorMessage);
                            if (!createdProject)
                            {
                                m_statusMessage = errorMessage.empty() ? "Failed to create project" : errorMessage;
                            }
                            else
                            {
                                m_project = std::move(createdProject);
                                applyProjectContext();
                                m_scene = CreateEmptyScene();
                                m_engine.SetScene(m_scene.get());
                                resetSelectionState();

                                const std::string startupScenePath = getDefaultProjectScenePath();
                                if (saveSceneToPath(startupScenePath))
                                {
                                    m_project->GetManifest().startupScene = m_project->MakeAssetReference(startupScenePath);
                                    saveCurrentProject("Created project: ");
                                }
                            }
                        }
                    }
                    if (ImGui::MenuItem("Open Project..."))
                    {
                        const std::string projectPath = ShowOpenFileDialog(kProjectFileFilter);
                        if (!projectPath.empty())
                        {
                            openProjectFromPath(projectPath);
                        }
                    }
                    if (ImGui::MenuItem("Save Project"))
                    {
                        saveCurrentProject("Saved project: ");
                    }

                    ImGui::Separator();
                    if (ImGui::MenuItem("New Scene"))
                    {
                        m_scene = CreateEmptyScene();
                        m_engine.SetScene(m_scene.get());
                        resetSelectionState();
                        m_statusMessage = "Created new scene";
                    }
                    if (ImGui::MenuItem("Open Scene..."))
                    {
                        const std::string filePath = ShowOpenFileDialog(kSceneFileFilter);
                        if (!filePath.empty())
                        {
                            std::string errorMessage;
                            auto loadedScene = scene::SceneSerializer::Load(filePath, &errorMessage);
                            if (loadedScene)
                            {
                                loadSceneIntoEditor(std::move(loadedScene), "Opened scene: " + std::filesystem::path(filePath).filename().string());
                            }
                            else
                            {
                                m_statusMessage = errorMessage.empty() ? "Failed to open scene" : errorMessage;
                            }
                        }
                    }
                    if (ImGui::MenuItem("Save Scene"))
                    {
                        if (m_scene)
                        {
                            std::string savePath = m_scene->GetFilePath();
                            if (savePath.empty())
                            {
                                savePath = ShowSaveFileDialog(kSceneFileFilter, getDefaultProjectScenePath(), "plutoscene");
                            }

                            if (!savePath.empty())
                            {
                                saveSceneToPath(savePath);
                            }
                        }
                    }
                    if (ImGui::MenuItem("Save Scene As..."))
                    {
                        if (m_scene)
                        {
                            const std::string suggestedPath = m_scene->GetFilePath().empty() ? getDefaultProjectScenePath() : m_scene->GetFilePath();
                            const std::string savePath = ShowSaveFileDialog(kSceneFileFilter, suggestedPath, "plutoscene");
                            if (!savePath.empty())
                            {
                                saveSceneToPath(savePath);
                            }
                        }
                    }
                    if (ImGui::MenuItem("Build Project..."))
                    {
                        if (!m_project)
                        {
                            m_statusMessage = "Open or create a project before building.";
                        }
                        else
                        {
                            bool canBuildProject = true;
                            const std::string mainSceneReference = m_project->GetManifest().startupScene;
                            if (mainSceneReference.empty())
                            {
                                m_statusMessage = "Set a main scene in Project > Project Settings before building.";
                                canBuildProject = false;
                            }

                            std::string mainScenePath;
                            if (canBuildProject)
                            {
                                if (!assets::Project::IsProjectAssetReference(mainSceneReference))
                                {
                                    m_statusMessage = "Project main scene must be stored inside the project Assets folder.";
                                    canBuildProject = false;
                                }
                                else
                                {
                                    mainScenePath = m_engine.GetAssetManager().ResolveAssetPath(mainSceneReference);
                                    if (mainScenePath.empty() || !std::filesystem::exists(mainScenePath))
                                    {
                                        m_statusMessage = "Project main scene could not be found on disk.";
                                        canBuildProject = false;
                                    }
                                }
                            }

                            if (canBuildProject && m_scene && !m_scene->GetFilePath().empty())
                            {
                                const std::string currentSceneReference = m_project->MakeAssetReference(m_scene->GetFilePath());
                                if (currentSceneReference == mainSceneReference && !saveSceneToPath(mainScenePath))
                                {
                                    canBuildProject = false;
                                }
                            }

                            std::unique_ptr<scene::Scene> buildScene;
                            if (canBuildProject)
                            {
                                std::string errorMessage;
                                buildScene = scene::SceneSerializer::Load(mainScenePath, &errorMessage);
                                if (!buildScene)
                                {
                                    m_statusMessage = errorMessage.empty() ? "Failed to load project main scene for build." : errorMessage;
                                    canBuildProject = false;
                                }
                            }

                            std::string validationError;
                            if (canBuildProject && !ValidateSceneForProjectExport(*buildScene, m_engine, &validationError))
                            {
                                m_statusMessage = validationError;
                                canBuildProject = false;
                            }

                            if (canBuildProject)
                            {
                                canBuildProject = saveCurrentProject("Saved project: ");
                            }

                            std::string destinationExecutablePath;
                            if (canBuildProject)
                            {
                                std::filesystem::path suggestedExecutablePath = m_project->GetRootDirectory() / "Builds" / m_project->GetManifest().name;
                                suggestedExecutablePath.replace_extension(".exe");
                                destinationExecutablePath = ShowSaveFileDialog(kExecutableFileFilter, suggestedExecutablePath.string(), "exe");
                                if (destinationExecutablePath.empty())
                                {
                                    canBuildProject = false;
                                }
                            }

                            std::filesystem::path runtimeExecutablePath;
                            if (canBuildProject)
                            {
                                RuntimeBuildInfo runtimeBuildInfo;
                                if (TryGetRuntimeBuildInfo(GetProcessExecutablePath(), runtimeBuildInfo))
                                {
                                    std::string runtimeBuildError;
                                    if (!BuildRuntimeExecutable(runtimeBuildInfo, &runtimeBuildError))
                                    {
                                        m_statusMessage = runtimeBuildError;
                                        canBuildProject = false;
                                    }
                                }
                            }

                            if (canBuildProject)
                            {
                                runtimeExecutablePath = FindBuiltRuntimeExecutable();
                                if (runtimeExecutablePath.empty())
                                {
                                    m_statusMessage = "PlutoGERuntime.exe was not found next to the current editor build. Build PlutoGERuntime in the same CMake configuration first.";
                                    canBuildProject = false;
                                }
                            }

                            if (canBuildProject)
                            {
                                std::string errorMessage;
                                if (assets::ExportStandaloneProject(*m_project, destinationExecutablePath, runtimeExecutablePath, &errorMessage))
                                {
                                    m_statusMessage = "Built project: " + std::filesystem::path(destinationExecutablePath).filename().string();
                                }
                                else
                                {
                                    m_statusMessage = errorMessage.empty() ? "Failed to build project" : errorMessage;
                                }
                            }
                        }
                    }
                    if (ImGui::MenuItem("Bake Scene"))
                    {
                        runBake(scene::SceneBakeSettings::BalancedPreview());
                    }
                    if (ImGui::MenuItem("Bake Scene Fast"))
                    {
                        runBake(scene::SceneBakeSettings::FastPreview());
                    }
                    if (ImGui::MenuItem("Bake Scene Final"))
                    {
                        runBake(scene::SceneBakeSettings::Final());
                    }
                    if (ImGui::MenuItem("Bake Scene Custom..."))
                    {
                        ImGui::OpenPopup("Bake Scene Custom");
                    }
                    ImGui::EndDisabled();

                    if (ImGui::MenuItem("Exit"))
                    {
                        window.Close();
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Project"))
                {
                    ImGui::BeginDisabled(!m_project || isBakeRunning);
                    if (ImGui::MenuItem("Project Settings..."))
                    {
                        projectSettingsPopupRequested = true;
                    }
                    ImGui::EndDisabled();
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("View"))
                {
                    if (ImGui::MenuItem("Editor Viewport", NULL, viewportPanel->IsOpen()))
                    {
                        viewportPanel->SetOpen(!viewportPanel->IsOpen());
                    }
                    if (ImGui::MenuItem("Game Viewport", NULL, viewportPanel2->IsOpen()))
                    {
                        viewportPanel2->SetOpen(!viewportPanel2->IsOpen());
                    }
                    if (ImGui::MenuItem("Scene Hierarchy", NULL, sceneHierarchyPanel->IsOpen()))
                    {
                        sceneHierarchyPanel->SetOpen(!sceneHierarchyPanel->IsOpen());
                    }
                    if (ImGui::MenuItem("Inspector", NULL, inspectorPanel->IsOpen()))
                    {
                        inspectorPanel->SetOpen(!inspectorPanel->IsOpen());
                    }
                    if (ImGui::MenuItem("Profiler", NULL, profilerPanel->IsOpen()))
                    {
                        profilerPanel->SetOpen(!profilerPanel->IsOpen());
                    }
                    ImGui::EndMenu();
                }
                if (!m_statusMessage.empty())
                {
                    ImGui::Separator();
                    ImGui::TextUnformatted(m_statusMessage.c_str());
                }
                ImGui::EndMainMenuBar();
            }

            if (projectSettingsPopupRequested)
            {
                openProjectSettingsPopup();
            }

            if (ImGui::BeginPopupModal("Project Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextUnformatted("Project Settings");
                ImGui::Separator();
                ImGui::InputText("Main Scene", projectMainSceneBuffer.data(), projectMainSceneBuffer.size());
                if (ImGui::Button("Browse..."))
                {
                    const std::string selectedScenePath = ShowOpenFileDialog(kSceneFileFilter);
                    if (!selectedScenePath.empty())
                    {
                        std::fill(projectMainSceneBuffer.begin(), projectMainSceneBuffer.end(), '\0');
                        strncpy_s(projectMainSceneBuffer.data(), projectMainSceneBuffer.size(), selectedScenePath.c_str(), _TRUNCATE);
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Use Current Scene"))
                {
                    if (!m_scene || m_scene->GetFilePath().empty())
                    {
                        m_statusMessage = "Save the scene before setting it as the project main scene.";
                    }
                    else
                    {
                        std::fill(projectMainSceneBuffer.begin(), projectMainSceneBuffer.end(), '\0');
                        strncpy_s(projectMainSceneBuffer.data(), projectMainSceneBuffer.size(), m_scene->GetFilePath().c_str(), _TRUNCATE);
                    }
                }

                ImGui::TextDisabled("The built executable loads this scene on startup.");

                if (ImGui::Button("Save"))
                {
                    std::string errorMessage;
                    const std::string mainSceneReference = makeProjectMainSceneReference(projectMainSceneBuffer.data(), &errorMessage);
                    if (mainSceneReference.empty())
                    {
                        m_statusMessage = errorMessage;
                    }
                    else
                    {
                        m_project->GetManifest().startupScene = mainSceneReference;
                        if (saveCurrentProject("Saved project: "))
                        {
                            m_statusMessage = "Saved project settings";
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            if (ImGui::BeginPopupModal("Bake Scene Custom", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                sanitizeBakeSettings(m_customBakeSettings);

                if (ImGui::Button("Fast Preset"))
                {
                    m_customBakeSettings = scene::SceneBakeSettings::FastPreview();
                }
                ImGui::SameLine();
                if (ImGui::Button("Balanced Preset"))
                {
                    m_customBakeSettings = scene::SceneBakeSettings::BalancedPreview();
                }
                ImGui::SameLine();
                if (ImGui::Button("Final Preset"))
                {
                    m_customBakeSettings = scene::SceneBakeSettings::Final();
                }

                ImGui::Separator();
                ImGui::InputInt("Lightmap Resolution", &m_customBakeSettings.lightmapResolution);
                ImGui::InputInt("Lightmap Tile Size", &m_customBakeSettings.lightmapTileSize);
                ImGui::Checkbox("Bake Indirect Bounce", &m_customBakeSettings.bakeIndirectBounce);
                ImGui::InputInt("Indirect Samples", &m_customBakeSettings.indirectBounceSampleCount);
                ImGui::SliderFloat("Lightmap Bounce Strength", &m_customBakeSettings.lightmapBounceStrength, 0.0f, 4.0f, "%.2f");
                ImGui::Checkbox("Bake Probe Volume", &m_customBakeSettings.bakeProbeVolume);
                ImGui::InputInt("Probe Directions", &m_customBakeSettings.probeDirectionCount);
                ImGui::SliderFloat("Probe Bounce Strength", &m_customBakeSettings.probeBounceStrength, 0.0f, 4.0f, "%.2f");

                sanitizeBakeSettings(m_customBakeSettings);

                ImGui::BeginDisabled(isBakeRunning);
                if (ImGui::Button("Bake"))
                {
                    runBake(m_customBakeSettings);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button("Close"))
                {
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }

            if (isBakeRunning)
            {
                ImGui::BeginDisabled();
            }
            m_panelManager.UpdatePanels();
            if (isBakeRunning)
            {
                ImGui::EndDisabled();
            }

            m_panelManager.EndPanelUpdate();

            const auto presentStart = std::chrono::high_resolution_clock::now();
            renderer.EndFrame();
            const auto presentEnd = std::chrono::high_resolution_clock::now();
            frameTimingStats.presentMs = std::chrono::duration<float, std::milli>(presentEnd - presentStart).count();

            const auto pollEventsStart = std::chrono::high_resolution_clock::now();
            window.PollEvents();
            const auto pollEventsEnd = std::chrono::high_resolution_clock::now();
            frameTimingStats.eventPollingMs = std::chrono::duration<float, std::milli>(pollEventsEnd - pollEventsStart).count();

            const auto frameEndTime = std::chrono::high_resolution_clock::now();
            m_profiler.SetLatestFrameTimingStats(frameTimingStats);
            m_profiler.AddFrameSample(std::chrono::duration<float, std::milli>(frameEndTime - currentTime).count());

            lastTime = currentTime;
        }

        if (isEditorCameraLookActive)
        {
            SetCursorCapture(windowHandle, false);
        }
    }

    void EditorShell::Shutdown()
    {
        if (m_activeBakeTask)
        {
            m_activeBakeTask->Cancel();
            m_activeBakeTask.reset();
        }
        m_selectedEntity = nullptr;
        m_project.reset();
        m_scene.reset();
        m_engine.SetScene(nullptr);
        m_panelManager.ShutdownPanels();
        m_engine.Shutdown();
    }
}