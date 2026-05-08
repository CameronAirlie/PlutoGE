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
#include "PlutoGE/render/passes/PostProcessPass.h"
#include "PlutoGE/render/passes/ShadowPass.h"
#include "PlutoGE/scene/components/LightComponent.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <iostream>
#include <vector>
#include <algorithm>

namespace PlutoGE::render
{
    namespace
    {
        bool EnsureRenderTargetSize(RenderTarget *renderTarget, int width, int height)
        {
            if (!renderTarget)
            {
                return false;
            }

            if (renderTarget->GetWidth() == width && renderTarget->GetHeight() == height && renderTarget->IsInitialized())
            {
                return true;
            }

            return renderTarget->Resize(width, height);
        }
    }

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
        glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

        auto geometryPass = new GeometryPass();
        geometryPass->Initialize();
        m_renderPasses.push_back(geometryPass);

        auto shadowPass = new ShadowPass();
        shadowPass->Initialize();
        m_shadowPass = shadowPass;

        auto lightingPass = new LightingPass();
        lightingPass->Initialize();
        m_renderPasses.push_back(lightingPass);

        auto postProcessPass = new PostProcessPass();
        postProcessPass->Initialize();
        m_renderPasses.push_back(postProcessPass);

        m_temporaryRenderTarget = new RenderTarget(RenderTargetConfig{extents.width, extents.height, glm::vec4(0.0f)});
        if (!m_temporaryRenderTarget->IsInitialized())
        {
            std::cerr << "Failed to initialize temporary render target" << std::endl;
            return false;
        }

        m_postProcessIntermediateRenderTarget = new RenderTarget(RenderTargetConfig{extents.width, extents.height, glm::vec4(0.0f)});
        if (!m_postProcessIntermediateRenderTarget->IsInitialized())
        {
            std::cerr << "Failed to initialize post process intermediate render target" << std::endl;
            return false;
        }

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

    void Renderer::UpdateShadowMaps(std::vector<scene::Light *> lights)
    {
        if (!m_isInitialized || !m_shadowPass)
            return;

        RenderContext ctx{
            .cameraData = {},
            .cameraComponent = nullptr,
            .renderTarget = nullptr,
            .temporaryRenderTarget = m_temporaryRenderTarget,
            .postProcessIntermediateRenderTarget = m_postProcessIntermediateRenderTarget,
            .renderCommands = &m_renderCommands,
            .lights = &lights,
            .gBuffer = &m_gBuffer,
            .postProcessDebugView = m_postProcessDebugView,
        };

        m_shadowPass->Execute(ctx);
    }

    void Renderer::RenderFrame(const scene::CameraComponent &cameraComponent, RenderTarget *renderTarget, std::vector<scene::Light *> lights)
    {
        if (!m_isInitialized)
            return;

        int renderWidth = 0;
        int renderHeight = 0;

        if (renderTarget)
        {
            renderWidth = renderTarget->GetWidth();
            renderHeight = renderTarget->GetHeight();
        }
        else if (m_config.window)
        {
            const auto extents = m_config.window->GetExtents();
            renderWidth = extents.width;
            renderHeight = extents.height;
        }

        if (renderWidth <= 0 || renderHeight <= 0)
        {
            return;
        }

        if (!EnsureRenderTargetSize(m_temporaryRenderTarget, renderWidth, renderHeight) ||
            !EnsureRenderTargetSize(m_postProcessIntermediateRenderTarget, renderWidth, renderHeight))
        {
            std::cerr << "Failed to resize post process render targets" << std::endl;
            return;
        }

        auto cameraData = cameraComponent.GetCameraData(renderWidth, renderHeight);

        sort(m_renderCommands.begin(), m_renderCommands.end(),
             [](const RenderCommand &a, const RenderCommand &b)
             {
                 return a.material < b.material;
             });

        RenderContext ctx{
            .cameraData = cameraData,
            .cameraComponent = &cameraComponent,
            .renderTarget = renderTarget,
            .temporaryRenderTarget = m_temporaryRenderTarget,
            .postProcessIntermediateRenderTarget = m_postProcessIntermediateRenderTarget,
            .renderCommands = &m_renderCommands,
            .lights = &lights,
            .gBuffer = &m_gBuffer,
            .postProcessDebugView = m_postProcessDebugView,
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
        if (m_temporaryRenderTarget)
        {
            m_temporaryRenderTarget->Cleanup();
            delete m_temporaryRenderTarget;
            m_temporaryRenderTarget = nullptr;
        }
        if (m_postProcessIntermediateRenderTarget)
        {
            m_postProcessIntermediateRenderTarget->Cleanup();
            delete m_postProcessIntermediateRenderTarget;
            m_postProcessIntermediateRenderTarget = nullptr;
        }
        if (m_shadowPass)
        {
            delete m_shadowPass;
            m_shadowPass = nullptr;
        }
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