#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Graphics.h"

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

        glViewport(0, 0, 800, 600); // Set viewport to match window size (can be dynamic)

        // Enable depth testing for 3D rendering
        glEnable(GL_DEPTH_TEST);
        // Disable face culling for debug
        glDisable(GL_CULL_FACE);

        m_isInitialized = true;
        return true;
    }

    void Renderer::BeginFrame()
    {
        // Prepare for rendering a new frame (e.g., clear buffers)

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f); // Clear to dark gray
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void Renderer::RenderFrame()
    {
        // Use shader and draw triangle with VAO
        if (!m_isInitialized)
            return;

        // Get draw list from the engine and render it here
        auto &engine = core::Engine::GetInstance();

        // Set up a basic camera: positioned at (0,0,3), looking at origin, up is +Y
        glm::mat4 view = glm::lookAt(
            glm::vec3(0.0f, 0.0f, 5.0f), // Camera position
            glm::vec3(0.0f, 0.0f, 0.0f), // Target
            glm::vec3(0.0f, 1.0f, 0.0f)  // Up
        );
        // Perspective projection: 45 deg fov, aspect ratio 4:3, near=0.1, far=100
        glm::mat4 projection = glm::perspective(
            glm::radians(60.0f),
            4.0f / 3.0f,
            0.1f,
            100.0f);
        CameraData cameraData{
            .view = view,
            .projection = projection};

        for (const auto &command : m_renderCommands)
        {
            // Pass the correct model matrix from the command
            Graphics::DrawMeshWithMaterial(command.mesh, command.material, command.model, &cameraData);
        }

        m_renderCommands.clear();
    }

    void Renderer::EndFrame()
    {
        // Finalize the frame (e.g., swap buffers)

        if (m_config.window)
        {
            glfwSwapBuffers(static_cast<GLFWwindow *>(m_config.window->GetWindow()));
        }
    }

    void Renderer::Shutdown()
    {
        // Clean up rendering resources here
        m_isInitialized = false;

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

    void Renderer::CleanupResources()
    {
        // Clean up any additional resources if needed
    }
}