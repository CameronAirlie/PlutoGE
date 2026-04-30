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

        m_geometryPassShader = Shader::CreateGeometryPassShader();
        m_lightingPassShader = Shader::CreateLightingPassShader();

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

    // Forward Implementation
    // void Renderer::RenderFrame(CameraData &cameraData, RenderTarget *renderTarget)
    // {
    //     // Use shader and draw triangle with VAO
    //     if (!m_isInitialized)
    //         return;

    //     // Get draw list from the engine and render it here
    //     auto &engine = core::Engine::GetInstance();

    //     for (const auto &command : m_renderCommands)
    //     {
    //         Graphics::DrawMeshWithMaterial(command.mesh, command.material, command.model, &cameraData);
    //     }

    //     Graphics::UnbindRenderTarget();
    // }

    // Deferred Implementation
    void Renderer::RenderFrame(CameraData &cameraData, RenderTarget *renderTarget)
    {
        if (!m_isInitialized)
            return;

        GeometryPass(cameraData, renderTarget);
        LightingPass(cameraData, renderTarget);
    }

    void Renderer::GeometryPass(CameraData &cameraData, RenderTarget *renderTarget)
    {
        if (!m_gBuffer.IsInitialized() ||
            m_gBuffer.GetWidth() != renderTarget->GetWidth() ||
            m_gBuffer.GetHeight() != renderTarget->GetHeight())
        {
            m_gBuffer.Cleanup();
            m_gBuffer.Initialize(renderTarget->GetWidth(), renderTarget->GetHeight());
        }

        m_gBuffer.Bind();
        glEnable(GL_DEPTH_TEST);
        glViewport(0, 0, m_gBuffer.GetWidth(), m_gBuffer.GetHeight());
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        m_geometryPassShader->Bind();

        for (const auto &command : m_renderCommands)
        {
            glBindVertexArray(command.mesh->GetVAO());
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, command.mesh->GetEBO());

            m_geometryPassShader->SetUniform("uModel", command.model);
            m_geometryPassShader->SetUniform("uView", cameraData.view);
            m_geometryPassShader->SetUniform("uProjection", cameraData.projection);

            m_geometryPassShader->SetUniform("uColor", glm::vec3(1.0f));
            m_geometryPassShader->SetUniform("uHasAlbedoTexture", 0.0f);

            glDrawElements(GL_TRIANGLES,
                           (GLsizei)command.mesh->GetIndexCount(),
                           GL_UNSIGNED_INT,
                           0);
        }

        m_gBuffer.Unbind();
        m_geometryPassShader->Unbind();

        // glBindFramebuffer(GL_READ_FRAMEBUFFER, m_gBuffer.GetFBO());
        // glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0); // Default framebuffer

        // glBlitFramebuffer(0, 0, m_gBuffer.GetWidth(), m_gBuffer.GetHeight(),
        //                   0, 0, m_gBuffer.GetWidth(), m_gBuffer.GetHeight(),
        //                   GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    void Renderer::LightingPass(CameraData &cameraData, RenderTarget *renderTarget)
    {

        glDisable(GL_DEPTH_TEST); // Disable depth testing for lighting pass
        Graphics::BindRenderTarget(renderTarget);

        m_lightingPassShader->Bind();

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_gBuffer.GetPositionTextureID());
        m_lightingPassShader->SetUniform("gPosition", 0);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_gBuffer.GetNormalTextureID());
        m_lightingPassShader->SetUniform("gNormal", 1);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, m_gBuffer.GetAlbedoTextureID());
        m_lightingPassShader->SetUniform("gAlbedoSpec", 2);

        glm::vec3 cameraPos = glm::vec3(glm::inverse(cameraData.view)[3]); // Extract camera position from view matrix
        m_lightingPassShader->SetUniform("uViewPos", cameraPos);
        m_lightingPassShader->SetUniform("uLight.Position", glm::vec3(0.5f, 1.0f, 0.6f));
        m_lightingPassShader->SetUniform("uLight.Color", glm::vec3(1.0f, 1.0f, 1.0f));

        // Draw a full-screen quad to apply lighting calculations in the fragment shader
        // auto material = new Material();
        // material->SetShader(m_lightingPassShader);
        // auto quadMesh = Mesh::QuadUV();

        glBindVertexArray(0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        // Graphics::DrawMeshWithMaterial(quadMesh, material);

        Graphics::UnbindRenderTarget();
    }

    void Renderer::ClearRenderCommands()
    {
        m_renderCommands.clear();
    }

    void Renderer::DrawRenderTarget(RenderTarget *renderTarget)
    {
        Graphics::DrawRenderTarget(renderTarget);
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