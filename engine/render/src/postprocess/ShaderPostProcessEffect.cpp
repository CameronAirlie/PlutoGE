#include "PlutoGE/render/postprocess/ShaderPostProcessEffect.h"

#include "PlutoGE/render/GBuffer.h"
#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Shader.h"

#include <glad/glad.h>

namespace PlutoGE::render
{
    namespace
    {
        void CopyDepthBuffer(RenderTarget *source, RenderTarget *destination)
        {
            if (!source || !destination)
            {
                return;
            }

            glBindFramebuffer(GL_READ_FRAMEBUFFER, source->GetFramebufferID());
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination->GetFramebufferID());
            glBlitFramebuffer(
                0, 0, source->GetWidth(), source->GetHeight(),
                0, 0, destination->GetWidth(), destination->GetHeight(),
                GL_DEPTH_BUFFER_BIT,
                GL_NEAREST);
        }
    }

    void ShaderPostProcessEffect::BeginApply(const PostProcessContext &context) const
    {
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);

        if (context.destinationRenderTarget)
        {
            if (context.copyDepthToDestination)
            {
                CopyDepthBuffer(context.sourceRenderTarget, context.destinationRenderTarget);
            }
            Graphics::BindRenderTarget(context.destinationRenderTarget);
            glViewport(0, 0, context.destinationRenderTarget->GetWidth(), context.destinationRenderTarget->GetHeight());
            return;
        }

        Graphics::UnbindRenderTarget();
        if (context.sourceRenderTarget)
        {
            glViewport(0, 0, context.sourceRenderTarget->GetWidth(), context.sourceRenderTarget->GetHeight());
        }
    }

    void ShaderPostProcessEffect::EndApply() const
    {
        Graphics::UnbindRenderTarget();
    }

    void ShaderPostProcessEffect::BindCommonInputs(Shader *shader, const PostProcessContext &context) const
    {
        if (!shader || !context.sourceRenderTarget)
        {
            return;
        }

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, context.sourceRenderTarget->GetColorTextureID());
        if (shader->HasUniform("uSceneTexture"))
        {
            shader->SetUniform("uSceneTexture", 0);
        }

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, context.sourceRenderTarget->GetDepthTextureID());
        if (shader->HasUniform("uSceneDepthTexture"))
        {
            shader->SetUniform("uSceneDepthTexture", 1);
        }

        if (context.renderContext.gBuffer)
        {
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, context.renderContext.gBuffer->GetPositionTextureID());
            if (shader->HasUniform("uScenePositionTexture"))
            {
                shader->SetUniform("uScenePositionTexture", 2);
            }

            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, context.renderContext.gBuffer->GetNormalTextureID());
            if (shader->HasUniform("uSceneNormalTexture"))
            {
                shader->SetUniform("uSceneNormalTexture", 3);
            }

            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, context.renderContext.gBuffer->GetAlbedoTextureID());
            if (shader->HasUniform("uSceneAlbedoTexture"))
            {
                shader->SetUniform("uSceneAlbedoTexture", 4);
            }

            glActiveTexture(GL_TEXTURE15);
            glBindTexture(GL_TEXTURE_2D, context.renderContext.gBuffer->GetEmissionTextureID());
            if (shader->HasUniform("uSceneEmissionTexture"))
            {
                shader->SetUniform("uSceneEmissionTexture", 15);
            }
        }

        if (shader->HasUniform("uDebugViewMode"))
        {
            shader->SetUniform("uDebugViewMode", static_cast<int>(context.renderContext.postProcessDebugView));
        }
    }

    void ShaderPostProcessEffect::DrawFullscreenTriangle() const
    {
        Graphics::DrawFullscreenTriangle();
    }
}
