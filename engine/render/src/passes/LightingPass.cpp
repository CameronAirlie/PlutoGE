#include "PlutoGE/render/passes/LightingPass.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/GBuffer.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/scene/components/LightComponent.h"

namespace PlutoGE::render
{
    namespace
    {
        constexpr int kPositionTextureSlot = 0;
        constexpr int kNormalTextureSlot = 1;
        constexpr int kAlbedoTextureSlot = 2;
        constexpr int kShadowMap2DTextureSlot = 3;
        constexpr int kShadowMapCubeTextureSlot = 4;
        constexpr int kAmbientPassMode = 0;
        constexpr int kLightPassMode = 1;
        constexpr std::size_t kLightingSetupStage = 0;
        constexpr std::size_t kLightingAmbientStage = 1;
        constexpr std::size_t kLightingAccumulationStage = 2;

        void BindLightingInputs(Shader *shader, GBuffer *gBuffer)
        {
            glActiveTexture(GL_TEXTURE0 + kPositionTextureSlot);
            glBindTexture(GL_TEXTURE_2D, gBuffer->GetPositionTextureID());
            shader->SetUniform("gPosition", kPositionTextureSlot);

            glActiveTexture(GL_TEXTURE0 + kNormalTextureSlot);
            glBindTexture(GL_TEXTURE_2D, gBuffer->GetNormalTextureID());
            shader->SetUniform("gNormal", kNormalTextureSlot);

            glActiveTexture(GL_TEXTURE0 + kAlbedoTextureSlot);
            glBindTexture(GL_TEXTURE_2D, gBuffer->GetAlbedoTextureID());
            shader->SetUniform("gAlbedoSpec", kAlbedoTextureSlot);

            glActiveTexture(GL_TEXTURE0 + kShadowMap2DTextureSlot);
            glBindTexture(GL_TEXTURE_2D, 0);
            shader->SetUniform("uShadowMap2D", kShadowMap2DTextureSlot);

            glActiveTexture(GL_TEXTURE0 + kShadowMapCubeTextureSlot);
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
            shader->SetUniform("uShadowMapCube", kShadowMapCubeTextureSlot);
        }

        bool BindShadowMapForLight(const scene::Light &light)
        {
            glActiveTexture(GL_TEXTURE0 + kShadowMap2DTextureSlot);
            glBindTexture(GL_TEXTURE_2D, 0);

            glActiveTexture(GL_TEXTURE0 + kShadowMapCubeTextureSlot);
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

            if (!light.castsShadows || !light.shadowMap)
            {
                return false;
            }

            auto *shadowMap = light.shadowMap.get();
            if (!shadowMap)
            {
                return false;
            }

            if (light.type == scene::LightType::Point)
            {
                if (shadowMap->GetType() != GL_TEXTURE_CUBE_MAP)
                {
                    return false;
                }

                glActiveTexture(GL_TEXTURE0 + kShadowMapCubeTextureSlot);
                glBindTexture(GL_TEXTURE_CUBE_MAP, shadowMap->GetTextureID());
                return true;
            }

            if (shadowMap->GetType() != GL_TEXTURE_2D)
            {
                return false;
            }

            glActiveTexture(GL_TEXTURE0 + kShadowMap2DTextureSlot);
            glBindTexture(GL_TEXTURE_2D, shadowMap->GetTextureID());
            return true;
        }

        void BindLightUniforms(Shader *shader, const scene::Light &light, bool hasShadowMap)
        {
            shader->SetUniform("uLight.Position", light.position);
            shader->SetUniform("uLight.Color", light.color);
            shader->SetUniform("uLight.Intensity", light.intensity);
            shader->SetUniform("uLight.Range", light.range);
            shader->SetUniform("uLight.Direction", light.direction);
            shader->SetUniform("uLight.Type", static_cast<int>(light.type));
            shader->SetUniform("uLight.CastsShadows", hasShadowMap ? 1 : 0);
            shader->SetUniform("uLight.LightSpaceMatrix", light.shadowMatrix);
            shader->SetUniform("uLight.ShadowFarPlane", light.shadowFarPlane);
        }
    }

    void LightingPass::Initialize()
    {
        m_lightingPassShader = Shader::CreateLightingPassShader();
    }

    void LightingPass::Execute(const RenderContext &ctx)
    {
        if (!m_lightingPassShader || !ctx.temporaryRenderTarget || !ctx.gBuffer || !ctx.lights)
        {
            return;
        }

        if (ctx.renderer)
        {
            int lightCount = 0;
            int shadowedLightCount = 0;
            for (auto *light : *ctx.lights)
            {
                if (!light)
                {
                    continue;
                }

                ++lightCount;
                if (light->castsShadows && light->shadowMap)
                {
                    ++shadowedLightCount;
                }
            }

            ctx.renderer->SetLightingPassCounters(lightCount, shadowedLightCount);
            ctx.renderer->BeginLightingStageTiming(kLightingSetupStage);
        }

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        Graphics::ClearRenderTarget(ctx.temporaryRenderTarget);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, ctx.gBuffer->GetFBO());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ctx.temporaryRenderTarget->GetFramebufferID());
        glBlitFramebuffer(
            0, 0, ctx.gBuffer->GetWidth(), ctx.gBuffer->GetHeight(),
            0, 0, ctx.temporaryRenderTarget->GetWidth(), ctx.temporaryRenderTarget->GetHeight(),
            GL_DEPTH_BUFFER_BIT,
            GL_NEAREST);

        Graphics::BindRenderTarget(ctx.temporaryRenderTarget);

        if (ctx.renderer)
        {
            ctx.renderer->EndLightingStageTiming(kLightingSetupStage);
            ctx.renderer->BeginLightingStageTiming(kLightingAmbientStage);
        }

        m_lightingPassShader->Bind();
        BindLightingInputs(m_lightingPassShader, ctx.gBuffer);

        const glm::vec3 cameraPos = glm::vec3(glm::inverse(ctx.cameraData.view)[3]);
        m_lightingPassShader->SetUniform("uViewPos", cameraPos);

        glDisable(GL_BLEND);
        m_lightingPassShader->SetUniform("uPassMode", kAmbientPassMode);
        glBindVertexArray(0);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        if (ctx.renderer)
        {
            ctx.renderer->EndLightingStageTiming(kLightingAmbientStage);
            ctx.renderer->BeginLightingStageTiming(kLightingAccumulationStage);
        }

        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_ONE, GL_ONE);

        for (auto *light : *ctx.lights)
        {
            if (!light)
            {
                continue;
            }

            const bool hasShadowMap = BindShadowMapForLight(*light);
            BindLightUniforms(m_lightingPassShader, *light, hasShadowMap);
            m_lightingPassShader->SetUniform("uPassMode", kLightPassMode);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }

        glDisable(GL_BLEND);
        if (ctx.renderer)
        {
            ctx.renderer->EndLightingStageTiming(kLightingAccumulationStage);
        }
        Graphics::UnbindRenderTarget();
    }
}