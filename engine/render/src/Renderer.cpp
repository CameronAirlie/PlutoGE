#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/render/passes/GeometryPass.h"
#include "PlutoGE/render/passes/LightingPass.h"
#include "PlutoGE/scene/components/LightComponent.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <iostream>
#include <vector>
#include <algorithm>

namespace PlutoGE::render
{
    void ResizeCallback(int width, int height)
    {
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

        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        {
            return false;
        }

        auto extents = window->GetExtents();
        glViewport(0, 0, extents.width, extents.height);

        glEnable(GL_DEPTH_TEST);

        auto geometryPass = new GeometryPass();
        geometryPass->Initialize();
        m_renderPasses.push_back(geometryPass);

        auto lightingPass = new LightingPass();
        lightingPass->Initialize();
        m_renderPasses.push_back(lightingPass);

        m_isInitialized = true;
        return true;
    }

    void Renderer::BeginFrame(RenderTarget *renderTarget)
    {
        if (!m_isInitialized)
            return;

        if (renderTarget)
        {
            Graphics::ClearRenderTarget(renderTarget);
            return;
        }

        if (m_config.window)
        {
            const auto extents = m_config.window->GetExtents();
            glViewport(0, 0, extents.width, extents.height);
        }

        Graphics::ClearRenderTarget(nullptr);
    }

    void Renderer::RenderFrame(CameraData &cameraData, RenderTarget *renderTarget, std::vector<scene::Light *> lights)
    {
        if (!m_isInitialized)
            return;

        sort(m_renderCommands.begin(), m_renderCommands.end(),
             [](const RenderCommand &a, const RenderCommand &b)
             {
                 return a.material < b.material;
             });

        RenderContext ctx{
            .cameraData = cameraData,
            .renderTarget = renderTarget,
            .renderCommands = &m_renderCommands,
            .lights = &lights,
            .gBuffer = &m_gBuffer,
        };

        for (auto *pass : m_renderPasses)
        {
            pass->Execute(ctx);
        }
    }

    void Renderer::ClearRenderCommands()
    {
        m_renderCommands.clear();
    }

    void Renderer::EndFrame(RenderTarget *renderTarget)
    {
        if (renderTarget)
        {
            Graphics::UnbindRenderTarget();
            return;
        }

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