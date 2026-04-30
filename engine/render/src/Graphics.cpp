#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/GBuffer.h"

#include <glad/glad.h>

namespace PlutoGE::render
{
    void Graphics::BindRenderTarget(RenderTarget *renderTarget)
    {
        if (renderTarget)
        {
            renderTarget->Bind();
        }
    }

    void Graphics::UnbindRenderTarget()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void Graphics::ClearRenderTarget(RenderTarget *renderTarget)
    {
        if (renderTarget)
        {
            BindRenderTarget(renderTarget);
            glm::vec4 color = renderTarget->GetClearColor();
            glClearColor(color.r, color.g, color.b, color.a);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }
        else
        {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f); // Default clear color
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }
    }

    void Graphics::BindFramebuffer(GLuint framebufferID)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, framebufferID);
    }
    void Graphics::UnbindFramebuffer()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}