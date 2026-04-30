#pragma once

#include "PlutoGE/platform/Window.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/GBuffer.h"
#include <glm/glm.hpp>
#include <iostream>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

namespace PlutoGE::scene
{
    class CameraComponent;
}

namespace PlutoGE::render
{
    class Material;
    class Mesh;
    class RenderTarget;

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

    struct RenderContext
    {
        CameraData cameraData;                      // Camera data for the current frame
        RenderTarget *renderTarget;                 // Render target for the current frame (nullptr for default framebuffer)
        std::vector<RenderCommand> *renderCommands; // List of render commands for the current frame
        GBuffer *gBuffer;                           // GBuffer for deferred rendering
    };

    class Shader;
    class Renderer
    {
    public:
        Renderer() = default;
        ~Renderer() = default;

        bool Initialize(const RendererConfig &config = RendererConfig());
        void BeginFrame(RenderTarget *renderTarget = nullptr);
        void RenderFrame(CameraData &cameraData, RenderTarget *renderTarget = nullptr);
        void EndFrame(RenderTarget *renderTarget = nullptr);
        void Shutdown(RenderTarget *renderTarget = nullptr);
        void ClearRenderCommands();

        void SetVSyncEnabled(bool enabled);

        void SubmitRenderCommand(const RenderCommand &command)
        {
            m_renderCommands.push_back(command);
        }

    private:
        RendererConfig m_config;
        bool m_isInitialized = false;

        GBuffer m_gBuffer;
        void GeometryPass(CameraData &cameraData, RenderTarget *renderTarget);
        void LightingPass(CameraData &cameraData, RenderTarget *renderTarget);
        Shader *m_geometryPassShader = nullptr;
        Shader *m_lightingPassShader = nullptr;

        void CleanupResources(RenderTarget *renderTarget = nullptr);
        std::vector<RenderCommand> m_renderCommands; // List of render commands for the current frame
    };
}