#include "PlutoGE/render/passes/PostProcessPass.h"

#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Graphics.h"

#include <glad/glad.h>
#include <iostream>

namespace PlutoGE::render
{
    void PostProcessPass::Initialize()
    {
        m_postProcessShader = Shader::CreatePostProcessShader();
    }

    void PostProcessPass::Execute(const RenderContext &ctx)
    {
        if (!m_postProcessShader || !ctx.temporaryRenderTarget)
        {
            return;
        }

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        Graphics::BindRenderTarget(ctx.renderTarget);

        m_postProcessShader->Bind();

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ctx.temporaryRenderTarget->GetColorTextureID());
        if (m_postProcessShader->HasUniform("uSceneTexture"))
        {
            m_postProcessShader->SetUniform("uSceneTexture", 0);
        }
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, ctx.temporaryRenderTarget->GetDepthTextureID());
        if (m_postProcessShader->HasUniform("uSceneDepthTexture"))
        {
            m_postProcessShader->SetUniform("uSceneDepthTexture", 1);
        }

        if (ctx.gBuffer)
        {
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, ctx.gBuffer->GetPositionTextureID());
            if (m_postProcessShader->HasUniform("uScenePositionTexture"))
            {
                m_postProcessShader->SetUniform("uScenePositionTexture", 2);
            }

            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, ctx.gBuffer->GetNormalTextureID());
            if (m_postProcessShader->HasUniform("uSceneNormalTexture"))
            {
                m_postProcessShader->SetUniform("uSceneNormalTexture", 3);
            }

            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, ctx.gBuffer->GetAlbedoTextureID());
            if (m_postProcessShader->HasUniform("uSceneAlbedoTexture"))
            {
                m_postProcessShader->SetUniform("uSceneAlbedoTexture", 4);
            }
        }

        if (m_postProcessShader->HasUniform("uDebugViewMode"))
        {
            m_postProcessShader->SetUniform("uDebugViewMode", static_cast<int>(ctx.postProcessDebugView));
        }

        glBindVertexArray(0);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        Graphics::UnbindRenderTarget();
    }

}