#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/panels/ViewportPanel.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/components/MeshComponent.h"

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
        m_panelManager.AddPanel(viewportPanel);
        auto *renderTarget = viewportPanel->GetRenderTarget();

        auto scene = std::make_unique<scene::Scene>();
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

        auto cameraData = render::CameraData{};
        cameraData.view = glm::lookAt(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        cameraData.position = glm::vec3(0.0f, 0.0f, 5.0f);

        while (!window.ShouldClose())
        {
            auto currentTime = std::chrono::high_resolution_clock::now();
            deltaTime = currentTime - lastTime;

            const auto renderTargetWidth = renderTarget->GetWidth();
            const auto renderTargetHeight = renderTarget->GetHeight();
            if (renderTargetWidth > 0 && renderTargetHeight > 0)
            {
                cameraData.projection = glm::perspective(
                    glm::radians(90.0f),
                    static_cast<float>(renderTargetWidth) / static_cast<float>(renderTargetHeight),
                    0.1f,
                    100.0f);
            }

            scene->Update(deltaTime.count());

            renderer.BeginFrame(renderTarget);

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