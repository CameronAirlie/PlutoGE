#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/panels/ViewportPanel.h"
#include "PlutoGE/ui/panels/SceneHierarchyPanel.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/CameraComponent.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <iostream>
#include <chrono>
#include <memory>

namespace PlutoGE::ui
{
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

    void EditorShell::Render()
    {
        auto &window = m_engine.GetWindow();
        auto &renderer = m_engine.GetRenderer();
        auto deltaTime = std::chrono::duration<float>::zero();
        auto lastTime = std::chrono::high_resolution_clock::now();

        PanelConfig viewportConfig{"Viewport"};
        auto *viewportPanel = new ViewportPanel(viewportConfig);
        viewportPanel->Initialize();

        auto *sceneHierarchyPanel = new SceneHierarchyPanel(PanelConfig{"Scene Hierarchy"});
        sceneHierarchyPanel->Initialize();

        m_panelManager.AddPanel(viewportPanel);
        m_panelManager.AddPanel(sceneHierarchyPanel);

        auto *renderTarget = viewportPanel->GetRenderTarget();

        auto scene = std::make_unique<scene::Scene>();

        sceneHierarchyPanel->SetScene(scene.get());

        auto cube = std::make_unique<scene::Entity>(scene::EntityConfig{
            .name = "Cube",
        });
        auto *material = m_engine.GetAssetManager().CreateDefaultMaterial();
        material->SetColor(glm::vec3(0.8f, 0.2f, 0.2f));
        auto mesh = render::Mesh::Cube();
        auto meshComponent = new scene::MeshComponent(scene::MeshComponentConfig{
            .mesh = mesh,
            .material = material,
        });
        cube->AddComponent(meshComponent);
        scene->AddEntity(cube.get());

        auto cameraEntity = std::make_unique<scene::Entity>(scene::EntityConfig{
            .name = "Camera",
        });
        auto camera = render::Camera(render::CameraConfig{
            .fovY = 45.0f,
            .nearPlane = 0.1f,
            .farPlane = 100.0f,
        });
        auto cameraComponent = new scene::CameraComponent(&camera);
        cameraEntity->AddComponent(cameraComponent);
        cameraEntity->SetPosition(glm::vec3(0.0f, 0.0f, 5.0f));
        scene->AddEntity(cameraEntity.get());

        while (!window.ShouldClose())
        {
            auto currentTime = std::chrono::high_resolution_clock::now();
            deltaTime = currentTime - lastTime;

            const auto renderTargetWidth = renderTarget->GetWidth();
            const auto renderTargetHeight = renderTarget->GetHeight();

            scene->Update(deltaTime.count());

            camera.SetFOV(45.0f + 10.0f * sinf(static_cast<float>(glfwGetTime()))); // Animate FOV for demonstration

            renderer.BeginFrame(renderTarget);

            auto cameraData = cameraComponent->GetCameraData(renderTargetWidth, renderTargetHeight);
            renderer.RenderFrame(cameraData, renderTarget);

            m_panelManager.BeginPanelUpdate();

            m_panelManager.UpdatePanels();

            m_panelManager.EndPanelUpdate();

            renderer.EndFrame();

            window.PollEvents();

            lastTime = currentTime;
        }
    }

    void EditorShell::Shutdown()
    {
        m_panelManager.ShutdownPanels();
        m_engine.Shutdown();
    }
}