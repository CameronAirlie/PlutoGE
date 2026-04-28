#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/scene/components/CameraComponent.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <iostream>
#include <vector>

namespace PlutoGE::render
{
    void ResizeCallback(int width, int height)
    {
        std::cout << "Window resized: " << width << "x" << height << std::endl;
        glViewport(0, 0, width, height);
    }

    bool Renderer::Initialize(const RendererConfig &config)
    {
        m_config = config;

        auto window = m_config.window;
        if (!window)
        {
            // Handle error: window pointer is null
            return false;
        }

        window->SetContextCurrent();
        window->SetResizeCallback(ResizeCallback);

        // Initialize rendering resources here
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        {
            // Handle error: failed to initialize OpenGL context
            return false;
        }

        auto extents = window->GetExtents();
        glViewport(0, 0, extents.width, extents.height); // Set viewport to match window size (can be dynamic)

        // Enable depth testing for 3D rendering
        glEnable(GL_DEPTH_TEST);
        // Disable face culling for debug
        glDisable(GL_CULL_FACE);

        m_isInitialized = true;
        return true;
    }

    void Renderer::BeginFrame(RenderTarget *renderTarget)
    {
        if (!m_isInitialized)
            return;

        Graphics::ClearRenderTarget(nullptr); // Clear default framebuffer first to avoid artifacts if render target is not cleared properly

        if (renderTarget)
            Graphics::ClearRenderTarget(renderTarget);
    }

    void Renderer::RenderFrame(CameraData &cameraData, RenderTarget *renderTarget)
    {
        // Use shader and draw triangle with VAO
        if (!m_isInitialized)
            return;

        // Get draw list from the engine and render it here
        auto &engine = core::Engine::GetInstance();

        for (const auto &command : m_renderCommands)
        {
            Graphics::DrawMeshWithMaterial(command.mesh, command.material, command.model, &cameraData);
        }

        Graphics::UnbindRenderTarget();

        m_renderCommands.clear();
    }

    void Renderer::DrawRenderTarget(RenderTarget *renderTarget)
    {
        Graphics::DrawRenderTarget(renderTarget);
    }

    void Renderer::EndFrame(RenderTarget *renderTarget)
    {
        // Finalize the frame (e.g., swap buffers)

        if (m_config.window)
        {
            glfwSwapBuffers(static_cast<GLFWwindow *>(m_config.window->GetWindow()));
        }
    }

    void Renderer::Shutdown(RenderTarget *renderTarget)
    {
        // Clean up rendering resources here
        m_isInitialized = false;
        CleanupResources(renderTarget);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    void Renderer::SetVSyncEnabled(bool enabled)
    {
        // Enable or disable VSync based on the 'enabled' parameter
        // This typically involves calling platform-specific APIs to set the swap interval

        if (enabled)
        {
            glfwSwapInterval(1); // Enable VSync
        }
        else
        {
            glfwSwapInterval(0); // Disable VSync
        }
    }

    void Renderer::CleanupResources(RenderTarget *renderTarget)
    {
        if (renderTarget)
        {
            renderTarget->Cleanup();
            delete renderTarget;
            renderTarget = nullptr;
        }
        else
        {
            // Clean up any other resources if needed
        }
    }
}