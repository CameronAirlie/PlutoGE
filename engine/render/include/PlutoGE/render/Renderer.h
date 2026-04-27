#pragma once

#include "PlutoGE/platform/Window.h"
#include "PlutoGE/render/Camera.h"
#include <glm/glm.hpp>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

namespace PlutoGE::render
{
    class Material;
    class Mesh;

    struct RendererConfig
    {
        // Future configuration options can be added here
        platform::Window *window = nullptr; // Pointer to the Window, set during initialization
    };

    struct RenderCommand
    {
        Material *material; // Material to use for rendering
        Mesh *mesh;         // Mesh to render
        glm::mat4 model;    // Model matrix for the object (position, rotation, scale)
    };

    class RenderTarget;
    class Renderer
    {
    public:
        Renderer() = default;
        ~Renderer() = default;

        bool Initialize(const RendererConfig &config = RendererConfig());
        void BeginFrame();
        void RenderFrame();
        void EndFrame();
        void Shutdown();

        void SetVSyncEnabled(bool enabled);

        void SubmitRenderCommand(const RenderCommand &command)
        {
            m_renderCommands.push_back(command);
        }

        void SetCamera(Camera *camera)
        {
            m_camera = camera;
        }

    private:
        RendererConfig m_config;
        RenderTarget *m_finalRenderTarget = nullptr; // Final render target for the frame
        Camera *m_camera = nullptr;
        bool m_isInitialized = false;

        void CleanupResources();
        std::vector<RenderCommand> m_renderCommands; // List of render commands for the current frame
    };
}