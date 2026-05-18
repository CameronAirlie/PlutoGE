#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/panels/ProfilerPanel.h"
#include "PlutoGE/ui/panels/ViewportPanel.h"
#include "PlutoGE/ui/panels/SceneHierarchyPanel.h"
#include "PlutoGE/ui/panels/InspectorPanel.h"
#include "PlutoGE/assets/Project.h"
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
#include <chrono>
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
        constexpr const char *kSceneFileFilter = "PlutoGE Scene\0*.plutoscene\0All Files\0*.*\0";
        constexpr const char *kProjectFileFilter = "PlutoGE Project\0*.plutoproject\0All Files\0*.*\0";
        constexpr const char *kExecutableFileFilter = "Executable\0*.exe\0All Files\0*.*\0";
        constexpr const char *kDefaultProjectFileName = "UntitledProject.plutoproject";
        constexpr const char *kDefaultProjectSceneRelativePath = "Scenes/Main.plutoscene";

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

            for (auto *entity : entities)
            {
                if (!entity || !entity->IsActive())
                {
                    continue;
                }

                if (auto *cameraComponent = entity->GetComponent<scene::CameraComponent>())
                {
                    if (cameraComponent->GetCamera())
                    {
                        return cameraComponent;
                    }
                }
            }

            return nullptr;
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
    }

    EditorShell::~EditorShell() = default;

    void EditorShell::InitializeEditorCamera()
    {
        m_editorCamera = EditorViewportCamera{};
        m_editorCamera.AddPostProcessEffectByType("RSM");
        m_editorCamera.AddPostProcessEffectByType("VolumetricFog");
        m_editorCamera.AddPostProcessEffectByType("LSAO");
        m_editorCamera.AddPostProcessEffectByType("ToneMapping");
        m_editorCamera.AddPostProcessEffectByType("ColorGrading");
        m_editorCamera.AddPostProcessEffectByType("SceneComposite");
        m_editorCamera.AddPostProcessEffectByType("GammaCorrection");
    }

    void EditorShell::ApplyProjectContext()
    {
        auto &assetManager = m_engine.GetAssetManager();
        if (m_project)
        {
            assetManager.SetProjectContext(m_project->GetRootDirectory().string(), m_project->GetManifest().assetDirectory);
            return;
        }

        assetManager.ClearProjectContext();
    }

    void EditorShell::UpdateWindowTitle()
    {
        std::string windowTitle = "PlutoGE Editor";
        if (m_project && !m_project->GetManifest().name.empty())
        {
            windowTitle += " - ";
            windowTitle += m_project->GetManifest().name;
        }

        m_engine.GetWindow().SetTitle(windowTitle);
    }

    void EditorShell::ResetSelection()
    {
        m_selectedEntity = nullptr;
        m_isEditorCameraSelected = false;
    }

    void EditorShell::SetScene(std::unique_ptr<scene::Scene> scene)
    {
        if (m_activeBakeTask)
        {
            m_activeBakeTask->Cancel();
            m_activeBakeTask.reset();
        }

        if (!scene)
        {
            scene = CreateEmptyScene();
        }

        m_scene = std::move(scene);
        m_engine.SetScene(m_scene.get());
        ResetSelection();
    }

    std::filesystem::path EditorShell::GetDefaultProjectScenePath() const
    {
        if (!m_project)
        {
            return {};
        }

        if (m_scene && !m_scene->GetFilePath().empty())
        {
            const auto currentScenePath = std::filesystem::path(m_scene->GetFilePath()).lexically_normal();
            if (m_project->IsInAssetDirectory(currentScenePath))
            {
                return currentScenePath;
            }
        }

        return (m_project->GetAssetDirectoryPath() / std::filesystem::path(kDefaultProjectSceneRelativePath)).lexically_normal();
    }

    std::filesystem::path EditorShell::GetDefaultExportExecutablePath() const
    {
        if (!m_project)
        {
            return {};
        }

        std::string projectName = m_project->GetManifest().name.empty() ? "PlutoGEProject" : m_project->GetManifest().name;
        std::filesystem::path executablePath = m_project->GetRootDirectory() / "Build" / projectName;
#ifdef _WIN32
        executablePath.replace_extension(".exe");
#endif
        return executablePath.lexically_normal();
    }

    bool EditorShell::SaveSceneToPath(const std::filesystem::path &scenePath)
    {
        if (!m_scene)
        {
            m_statusMessage = "No scene to save.";
            return false;
        }

        const auto normalizedScenePath = std::filesystem::absolute(scenePath).lexically_normal();
        std::error_code errorCode;
        std::filesystem::create_directories(normalizedScenePath.parent_path(), errorCode);
        if (errorCode)
        {
            m_statusMessage = "Failed to create scene directory: " + normalizedScenePath.parent_path().string();
            return false;
        }

        std::string errorMessage;
        if (!scene::SceneSerializer::Save(*m_scene, normalizedScenePath.string(), &errorMessage))
        {
            m_statusMessage = errorMessage.empty() ? "Failed to save scene" : errorMessage;
            return false;
        }

        m_scene->SetFilePath(normalizedScenePath.string());
        m_statusMessage = "Saved scene: " + normalizedScenePath.filename().string();
        return true;
    }

    bool EditorShell::SaveActiveSceneIntoProject()
    {
        if (!m_project)
        {
            return true;
        }

        if (!m_scene)
        {
            SetScene(CreateEmptyScene());
        }

        const auto projectScenePath = GetDefaultProjectScenePath();
        if (projectScenePath.empty())
        {
            m_statusMessage = "Project scene path could not be determined.";
            return false;
        }

        if (!SaveSceneToPath(projectScenePath))
        {
            return false;
        }

        m_project->GetManifest().startupScene = m_project->MakeAssetReference(projectScenePath);
        return true;
    }

    bool EditorShell::LoadProjectFromPath(const std::filesystem::path &manifestPath)
    {
        std::string errorMessage;
        auto loadedProject = assets::Project::Load(manifestPath, &errorMessage);
        if (!loadedProject)
        {
            m_statusMessage = errorMessage.empty() ? "Failed to open project." : errorMessage;
            return false;
        }

        m_project = std::move(loadedProject);
        ApplyProjectContext();

        const auto &manifest = m_project->GetManifest();
        m_editorCamera.position = glm::vec3(manifest.editorCamera.positionX, manifest.editorCamera.positionY, manifest.editorCamera.positionZ);
        m_editorCamera.yawDegrees = manifest.editorCamera.yawDegrees;
        m_editorCamera.pitchDegrees = manifest.editorCamera.pitchDegrees;
        m_engine.GetRenderer().SetVSyncEnabled(manifest.vSyncEnabled);

        std::unique_ptr<scene::Scene> loadedScene;
        if (!manifest.startupScene.empty())
        {
            const std::string startupScenePath = m_engine.GetAssetManager().ResolveAssetPath(manifest.startupScene);
            if (!startupScenePath.empty() && std::filesystem::exists(startupScenePath))
            {
                loadedScene = scene::SceneSerializer::Load(startupScenePath, &errorMessage);
            }
        }

        if (loadedScene)
        {
            SetScene(std::move(loadedScene));
            m_statusMessage = "Opened project: " + manifest.name;
        }
        else
        {
            SetScene(CreateEmptyScene());
            if (!errorMessage.empty())
            {
                m_statusMessage = errorMessage;
            }
            else
            {
                m_statusMessage = "Opened project without a startup scene.";
            }
        }

        UpdateWindowTitle();
        return true;
    }

    bool EditorShell::CreateProjectAtPath(const std::filesystem::path &manifestPath)
    {
        std::string errorMessage;
        std::string projectName = manifestPath.stem().string();
        if (projectName.empty())
        {
            projectName = "UntitledProject";
        }

        auto createdProject = assets::Project::Create(manifestPath, projectName, &errorMessage);
        if (!createdProject)
        {
            m_statusMessage = errorMessage.empty() ? "Failed to create project." : errorMessage;
            return false;
        }

        m_project = std::move(createdProject);
        ApplyProjectContext();
        SetScene(CreateEmptyScene());

        if (!SaveProjectToDisk())
        {
            return false;
        }

        m_statusMessage = "Created project: " + m_project->GetManifest().name;
        UpdateWindowTitle();
        return true;
    }

    bool EditorShell::SaveProjectToDisk()
    {
        if (!m_project)
        {
            m_statusMessage = "No project loaded.";
            return false;
        }

        ApplyProjectContext();

        auto &manifest = m_project->GetManifest();
        manifest.editorCamera.positionX = m_editorCamera.position.x;
        manifest.editorCamera.positionY = m_editorCamera.position.y;
        manifest.editorCamera.positionZ = m_editorCamera.position.z;
        manifest.editorCamera.yawDegrees = m_editorCamera.yawDegrees;
        manifest.editorCamera.pitchDegrees = m_editorCamera.pitchDegrees;
        if (manifest.windowTitle.empty())
        {
            manifest.windowTitle = manifest.name;
        }

        if (!SaveActiveSceneIntoProject())
        {
            return false;
        }

        m_project->RefreshAssetRegistry();

        std::string errorMessage;
        if (!m_project->Save(&errorMessage))
        {
            m_statusMessage = errorMessage.empty() ? "Failed to save project." : errorMessage;
            return false;
        }

        m_statusMessage = "Saved project: " + m_project->GetManifestPath().filename().string();
        UpdateWindowTitle();
        return true;
    }

    bool EditorShell::BuildProjectToPath(const std::filesystem::path &destinationExecutablePath)
    {
        if (!m_project)
        {
            m_statusMessage = "No project loaded.";
            return false;
        }

        if (!SaveProjectToDisk())
        {
            return false;
        }

        const auto runtimeExecutablePath = assets::FindRuntimeExecutable(std::filesystem::current_path());
        if (runtimeExecutablePath.empty())
        {
            m_statusMessage = "Could not find PlutoGERuntime executable to export.";
            return false;
        }

        std::string errorMessage;
        if (!assets::ExportStandaloneProject(*m_project, destinationExecutablePath, runtimeExecutablePath, &errorMessage))
        {
            m_statusMessage = errorMessage.empty() ? "Failed to build project." : errorMessage;
            return false;
        }

        m_statusMessage = "Built project: " + std::filesystem::path(destinationExecutablePath).filename().string();
        return true;
    }

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

        InitializeEditorCamera();
        ApplyProjectContext();
        SetScene(CreateEmptyScene());
        m_statusMessage = "Ready";
        UpdateWindowTitle();

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
                        const std::string projectPath = ShowSaveFileDialog(kProjectFileFilter, kDefaultProjectFileName, "plutoproject");
                        if (!projectPath.empty())
                        {
                            CreateProjectAtPath(projectPath);
                        }
                    }
                    if (ImGui::MenuItem("Open Project..."))
                    {
                        const std::string projectPath = ShowOpenFileDialog(kProjectFileFilter);
                        if (!projectPath.empty())
                        {
                            LoadProjectFromPath(projectPath);
                        }
                    }
                    if (ImGui::MenuItem("Save Project", nullptr, false, m_project != nullptr))
                    {
                        SaveProjectToDisk();
                    }
                    if (ImGui::MenuItem("Build Project...", nullptr, false, m_project != nullptr))
                    {
                        const std::string suggestedPath = GetDefaultExportExecutablePath().empty()
                                                              ? std::string("PlutoGERuntime.exe")
                                                              : GetDefaultExportExecutablePath().string();
                        const std::string exportPath = ShowSaveFileDialog(kExecutableFileFilter, suggestedPath, "exe");
                        if (!exportPath.empty())
                        {
                            BuildProjectToPath(exportPath);
                        }
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("New Scene"))
                    {
                        SetScene(CreateEmptyScene());
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
                                SetScene(std::move(loadedScene));
                                m_statusMessage = "Opened scene: " + std::filesystem::path(filePath).filename().string();
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
                                savePath = ShowSaveFileDialog(kSceneFileFilter,
                                                              m_project ? GetDefaultProjectScenePath().string() : std::string("scene.plutoscene"),
                                                              "plutoscene");
                            }

                            if (!savePath.empty())
                            {
                                SaveSceneToPath(savePath);
                            }
                        }
                    }
                    if (ImGui::MenuItem("Save Scene As..."))
                    {
                        if (m_scene)
                        {
                            const std::string suggestedPath = m_scene->GetFilePath().empty() ? "scene.plutoscene" : m_scene->GetFilePath();
                            const std::string savePath = ShowSaveFileDialog(kSceneFileFilter, suggestedPath, "plutoscene");
                            if (!savePath.empty())
                            {
                                SaveSceneToPath(savePath);
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
        m_engine.GetAssetManager().ClearProjectContext();
        m_engine.SetScene(nullptr);
        m_panelManager.ShutdownPanels();
        m_engine.Shutdown();
    }
}