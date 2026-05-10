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

namespace PlutoGE::ui
{
    namespace
    {
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
        viewportConfig.name = "Viewport";
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
        auto texture = m_engine.GetAssetManager().LoadTexture("C:/textures/brick/brick.png");
        material2->SetAlbedoTexture(texture);
        auto normalTexture = m_engine.GetAssetManager().LoadTexture("C:/textures/brick/brick_normal.png");
        material2->SetNormalTexture(normalTexture);
        material2->SetFlipNormalY(true);
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

        auto cameraEntity = std::make_unique<scene::Entity>(scene::EntityConfig{
            .name = "Camera",
        });
        auto camera = render::Camera(render::CameraConfig{
            .fovY = 45.0f,
            .nearPlane = 0.1f,
            .farPlane = 100.0f,
        });
        cameraEntity->CreateComponent<scene::CameraComponent>(&camera);
        cameraEntity->SetPosition(glm::vec3(0.0f, 0.0f, 5.0f));
        auto *cameraEntityPtr = scene->AddEntity(std::move(cameraEntity));

        auto cameraEntity2 = std::make_unique<scene::Entity>(scene::EntityConfig{
            .name = "Camera 2",
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

        for (int i = 0; i < 1; ++i)
        {
            auto lightEntity = std::make_unique<scene::Entity>(scene::EntityConfig{
                .name = "Key Light " + std::to_string(i),
            });
            lightEntity->SetPosition(glm::vec3(0.0f, 5.0f, 0.0f));
            lightEntity->SetRotation(glm::vec3(-50.0f, -35.0f, 0.0f));
            auto *lightComponent = lightEntity->CreateComponent<scene::LightComponent>();
            lightComponent->SetColor(glm::vec3(1.0f, 0.95f, 0.85f));
            lightComponent->SetIntensity(1.0f);
            lightComponent->SetRange(20.0f);
            lightComponent->SetLightType(scene::LightType::Directional);
            lightComponent->SetCastsShadows(true);
            scene->AddEntity(std::move(lightEntity));
        }

        ViewportPanelConfig viewportConfig2;
        viewportConfig2.name = "Viewport 2";
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

        renderer.SetVSyncEnabled(true);

        while (!window.ShouldClose())
        {
            auto currentTime = std::chrono::high_resolution_clock::now();
            deltaTime = currentTime - lastTime;
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

            camera.SetFOV(45.0f + 10.0f * sinf(static_cast<float>(glfwGetTime()))); // Animate FOV for demonstration

            auto *cameraComponent = cameraEntityPtr->GetComponent<scene::CameraComponent>();
            auto *cameraComponent2 = cameraEntity2Ptr->GetComponent<scene::CameraComponent>();
            const bool shouldRenderViewport1 = viewportPanel->ShouldRenderFrame() && IsCameraActiveInScene(scene.get(), cameraComponent);
            const bool shouldRenderViewport2 = viewportPanel2->ShouldRenderFrame() && IsCameraActiveInScene(scene.get(), cameraComponent2);

            const auto viewportRenderStart = std::chrono::high_resolution_clock::now();
            if (shouldRenderViewport1)
            {
                ++frameTimingStats.renderedViewportCount;
                viewportPanel->RenderFrame(*cameraComponent);
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
                    if (ImGui::MenuItem("Viewport 1", NULL, viewportPanel->IsOpen()))
                    {
                        viewportPanel->SetOpen(!viewportPanel->IsOpen());
                    }
                    if (ImGui::MenuItem("Viewport 2", NULL, viewportPanel2->IsOpen()))
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
    }

    void EditorShell::Shutdown()
    {
        m_panelManager.ShutdownPanels();
        m_engine.Shutdown();
    }
}