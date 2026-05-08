#include "PlutoGE/render/passes/LightingPass.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/GBuffer.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/scene/components/LightComponent.h"

#include <algorithm>
#include <string>

namespace PlutoGE::render
{
    namespace
    {
        constexpr int kMaxDeferredLights = 16;
        constexpr int kMax2DShadowMaps = 8;
        constexpr int kMaxCubeShadowMaps = 4;
        constexpr int kReservedLightingTextureSlots = 3;
    }

    void LightingPass::Initialize()
    {
        m_lightingPassShader = Shader::CreateLightingPassShader();
    }

    void LightingPass::Execute(const RenderContext &ctx)
    {
        if (!ctx.temporaryRenderTarget)
        {
            return;
        }

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        Graphics::ClearRenderTarget(ctx.temporaryRenderTarget);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, ctx.gBuffer->GetFBO());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ctx.temporaryRenderTarget->GetFramebufferID());
        // Get depth from GBuffer and copy to temporary render target's depth attachment
        glBlitFramebuffer(
            0, 0, ctx.gBuffer->GetWidth(), ctx.gBuffer->GetHeight(),
            0, 0, ctx.temporaryRenderTarget->GetWidth(), ctx.temporaryRenderTarget->GetHeight(),
            GL_DEPTH_BUFFER_BIT,
            GL_NEAREST);

        Graphics::BindRenderTarget(ctx.temporaryRenderTarget);

        auto lights = *ctx.lights;

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

        const int lightCount = std::min<int>(static_cast<int>(lights.size()), kMaxDeferredLights);
        m_lightingPassShader->SetUniform("uLightCount", lightCount);

        int next2DShadowIndex = 0;
        int nextCubeShadowIndex = 0;
        int nextShadowTextureSlot = kReservedLightingTextureSlots;

        for (int index = 0; index < lightCount; ++index)
        {
            auto *light = lights[index];
            if (!light)
            {
                continue;
            }

            light->shadowTextureType = 0;
            light->shadowMapIndex = -1;
            light->shadowTextureSlot = -1;

            const std::string uniformPrefix = "uLights[" + std::to_string(index) + "]";
            m_lightingPassShader->SetUniform(uniformPrefix + ".Position", light->position);
            m_lightingPassShader->SetUniform(uniformPrefix + ".Color", light->color);
            m_lightingPassShader->SetUniform(uniformPrefix + ".Intensity", light->intensity);
            m_lightingPassShader->SetUniform(uniformPrefix + ".Range", light->range);
            m_lightingPassShader->SetUniform(uniformPrefix + ".Direction", light->direction);
            m_lightingPassShader->SetUniform(uniformPrefix + ".Type", static_cast<int>(light->type));

            if (light->castsShadows && light->shadowMap)
            {
                if (light->type == scene::LightType::Point)
                {
                    if (nextCubeShadowIndex < kMaxCubeShadowMaps)
                    {
                        light->shadowTextureType = 2;
                        light->shadowMapIndex = nextCubeShadowIndex++;
                        light->shadowTextureSlot = nextShadowTextureSlot++;
                        glActiveTexture(GL_TEXTURE0 + light->shadowTextureSlot);
                        glBindTexture(light->shadowMap->GetType(), light->shadowMap->GetTextureID());
                        m_lightingPassShader->SetUniform("uShadowMapsCube[" + std::to_string(light->shadowMapIndex) + "]", light->shadowTextureSlot);
                    }
                }
                else if (next2DShadowIndex < kMax2DShadowMaps)
                {
                    light->shadowTextureType = 1;
                    light->shadowMapIndex = next2DShadowIndex++;
                    light->shadowTextureSlot = nextShadowTextureSlot++;
                    glActiveTexture(GL_TEXTURE0 + light->shadowTextureSlot);
                    glBindTexture(light->shadowMap->GetType(), light->shadowMap->GetTextureID());
                    m_lightingPassShader->SetUniform("uShadowMaps2D[" + std::to_string(light->shadowMapIndex) + "]", light->shadowTextureSlot);
                }
            }

            m_lightingPassShader->SetUniform(uniformPrefix + ".CastsShadows", light->shadowTextureType == 0 ? 0 : 1);
            m_lightingPassShader->SetUniform(uniformPrefix + ".ShadowTextureType", light->shadowTextureType);
            m_lightingPassShader->SetUniform(uniformPrefix + ".ShadowMapIndex", light->shadowMapIndex);
            m_lightingPassShader->SetUniform(uniformPrefix + ".LightSpaceMatrix", light->shadowMatrix);
            m_lightingPassShader->SetUniform(uniformPrefix + ".ShadowFarPlane", light->shadowFarPlane);
        }

        glBindVertexArray(0);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        Graphics::UnbindRenderTarget();
    }
}