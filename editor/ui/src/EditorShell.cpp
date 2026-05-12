#include "PlutoGE/ui/EditorShell.h"
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

namespace PlutoGE::ui
{
    namespace
    {
        constexpr float kEditorCameraMoveSpeed = 6.0f;
        constexpr float kEditorCameraBoostMultiplier = 2.5f;
        constexpr float kEditorCameraMouseSensitivity = 0.12f;
        constexpr float kEditorCameraPitchLimitDegrees = 89.0f;

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

        m_editorCamera = EditorViewportCamera{};
        m_editorCamera.AddPostProcessEffectByType("SSGI");
        m_editorCamera.AddPostProcessEffectByType("ToneMapping");
        m_editorCamera.AddPostProcessEffectByType("SceneComposite");
        m_editorCamera.AddPostProcessEffectByType("GammaCorrection");

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
        auto &scriptEngine = m_engine.GetScriptEngine();
        auto deltaTime = std::chrono::duration<float>::zero();
        auto lastTime = std::chrono::high_resolution_clock::now();

        ViewportPanelConfig viewportConfig;
        viewportConfig.name = "Editor Viewport";
        viewportConfig.clearColor = glm::vec4(0.1f, 0.1f, 0.15f, 1.0f);
        auto *viewportPanel = new ViewportPanel(viewportConfig);
        viewportPanel->Initialize();

        auto *sceneHierarchyPanel = new SceneHierarchyPanel(PanelConfig{"Scene Hierarchy"});
        sceneHierarchyPanel->Initialize();

        m_panelManager.AddPanel(viewportPanel);
        m_panelManager.AddPanel(sceneHierarchyPanel);

        auto *renderTarget = viewportPanel->GetRenderTarget();

        auto scene = std::make_unique<scene::Scene>();
        m_engine.SetScene(scene.get());

        auto cube = std::make_unique<scene::Entity>(scene::EntityConfig{
            .name = "Cube",
        });
        auto *material = m_engine.GetAssetManager().CreateDefaultMaterial();
        material->SetColor(glm::vec4(0.8f, 0.2f, 0.2f, 1.0f));
        auto mesh = render::Mesh::Cube();
        cube->CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{
            .mesh = mesh,
            .material = material,
        });
        cube->SetPosition(glm::vec3(0.0f, -1.0f, 0.0f));
        cube->SetScale(glm::vec3(10.0f, 0.1f, 10.0f));
        auto *cubeEntity = scene->AddEntity(std::move(cube));

        auto cube2 = std::make_unique<scene::Entity>(scene::EntityConfig{
            .name = "Cube 2",
        });
        auto *material2 = m_engine.GetAssetManager().CreateDefaultMaterial();
        auto texture = m_engine.GetAssetManager().LoadTexture("C:/textures/cobble/cobble_base.png");
        material2->SetAlbedoTexture(texture);
        auto normalTexture = m_engine.GetAssetManager().LoadTexture("C:/textures/cobble/cobble_normal.png");
        material2->SetNormalTexture(normalTexture);
        auto roughnessTexture = m_engine.GetAssetManager().LoadTexture("C:/textures/cobble/cobble_roughness.png");
        material2->SetRoughnessTexture(roughnessTexture);
        // material2->SetColor(glm::vec4(0.8f, 0.2f, 0.2f, 1.0f));
        auto mesh2 = render::Mesh::Cube();
        cube2->CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{
            .mesh = mesh2,
            .material = material2,
        });
        scene->AddEntity(std::move(cube2));

        auto testEntity = std::make_unique<scene::Entity>(scene::EntityConfig{
            .name = "Test Entity",
        });
        testEntity->SetParent(cubeEntity);

        auto cameraEntity2 = std::make_unique<scene::Entity>(scene::EntityConfig{
            .name = "Game Camera",
        });
        auto camera2 = render::Camera(render::CameraConfig{
            .fovY = 60.0f,
            .nearPlane = 0.1f,
            .farPlane = 100.0f,
        });
        cameraEntity2->CreateComponent<scene::CameraComponent>(&camera2);
        cameraEntity2->SetPosition(glm::vec3(0.0f, 0.0f, 5.0f));

        auto cameraHolder = std::make_unique<scene::Entity>(scene::EntityConfig{
            .name = "Camera Holder",
        });
        cameraHolder->SetPosition(glm::vec3(0.0f, 0.0f, 0.0f));
        auto *cameraHolderEntity = scene->AddEntity(std::move(cameraHolder));
        auto *cameraEntity2Ptr = scene->AddEntity(std::move(cameraEntity2), cameraHolderEntity);

        for (int i = 0; i < 10; ++i)
        {
            auto lightEntity = std::make_unique<scene::Entity>(scene::EntityConfig{
                .name = "Key Light " + std::to_string(i),
            });
            auto randomPosition = (randomColour() - 0.5f) * 10.0f;
            lightEntity->SetPosition(glm::vec3(randomPosition.x, 5.0f, randomPosition.z));
            // lightEntity->SetRotation(glm::vec3(-50.0f, -35.0f, 0.0f));
            auto *lightComponent = lightEntity->CreateComponent<scene::LightComponent>();
            lightComponent->SetColor(randomColour());
            lightComponent->SetIntensity(5.0f);
            lightComponent->SetRange(20.0f);
            lightComponent->SetLightType(scene::LightType::Point);
            lightComponent->SetCastsShadows(true);
            scene->AddEntity(std::move(lightEntity));
        }

        ViewportPanelConfig viewportConfig2;
        viewportConfig2.name = "Game Viewport";
        viewportConfig2.openByDefault = true;
        viewportConfig2.clearColor = glm::vec4(0.15f, 0.1f, 0.1f, 1.0f);
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

            // Scene update

            const auto sceneUpdateStart = std::chrono::high_resolution_clock::now();
            m_engine.UpdateAsyncMeshImports();
            scene->Update(deltaTime.count());
            const auto sceneUpdateEnd = std::chrono::high_resolution_clock::now();
            frameTimingStats.sceneUpdateMs = std::chrono::duration<float, std::milli>(sceneUpdateEnd - sceneUpdateStart).count();

            UpdateEditorCamera(m_editorCamera,
                               windowHandle,
                               viewportPanel->IsViewportHovered() || viewportPanel->IsViewportFocused(),
                               deltaSeconds,
                               isEditorCameraLookActive,
                               lastEditorCameraCursorX,
                               lastEditorCameraCursorY);

            auto *cameraComponent2 = cameraEntity2Ptr->GetComponent<scene::CameraComponent>();
            const bool shouldRenderViewport1 = viewportPanel->ShouldRenderFrame();
            const bool shouldRenderViewport2 = viewportPanel2->ShouldRenderFrame() && IsCameraActiveInScene(scene.get(), cameraComponent2);

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

                renderer.RenderFrame(editorCameraData, renderTarget, scene->GetLights(), &editorPostProcessEffects);
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

            // Toolbar menu
            if (ImGui::BeginMainMenuBar())
            {
                if (ImGui::BeginMenu("File"))
                {
                    if (ImGui::MenuItem("New Scene"))
                    {
                        scene = std::make_unique<scene::Scene>();
                        m_engine.SetScene(scene.get());
                    }
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
                ImGui::EndMainMenuBar();
            }

            m_panelManager.UpdatePanels();

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
        m_panelManager.ShutdownPanels();
        m_engine.Shutdown();
    }
}