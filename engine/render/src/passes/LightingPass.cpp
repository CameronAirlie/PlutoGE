#include "PlutoGE/render/passes/LightingPass.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/GBuffer.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Graphics.h"

namespace PlutoGE::render
{
    void LightingPass::Initialize()
    {
        m_lightingPassShader = Shader::CreateLightingPassShader();
    }
    

    void LightingPass::Execute(const RenderContext &ctx)
    {
        glDisable(GL_DEPTH_TEST); // Disable depth testing for lighting pass
        Graphics::BindRenderTarget(ctx.renderTarget);

        m_lightingPassShader->Bind();

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ctx.gBuffer->GetPositionTextureID());
        m_lightingPassShader->SetUniform("gPosition", 0);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, ctx.gBuffer->GetNormalTextureID());
        m_lightingPassShader->SetUniform("gNormal", 1);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, ctx.gBuffer->GetAlbedoTextureID());
        m_lightingPassShader->SetUniform("gAlbedoSpec", 2);

        glm::vec3 cameraPos = glm::vec3(glm::inverse(ctx.cameraData.view)[3]); // Extract camera position from view matrix
        m_lightingPassShader->SetUniform("uViewPos", cameraPos);
        m_lightingPassShader->SetUniform("uLight.Position", glm::vec3(0.5f, 1.0f, 0.6f));
        m_lightingPassShader->SetUniform("uLight.Color", glm::vec3(1.0f, 1.0f, 1.0f));

        glBindVertexArray(0);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        Graphics::UnbindRenderTarget();
    }
}