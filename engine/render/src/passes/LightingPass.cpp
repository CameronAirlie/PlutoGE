#include "PlutoGE/render/passes/LightingPass.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/GBuffer.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/postprocess/RSMEffect.h"
#include "PlutoGE/render/postprocess/SceneCompositeEffect.h"
#include "PlutoGE/render/postprocess/SSGIEffect.h"
#include "PlutoGE/render/passes/LightPropagationVolumePass.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/Scene.h"

namespace PlutoGE::render
{
    namespace
    {
        constexpr int kPositionTextureSlot = 0;
        constexpr int kNormalTextureSlot = 1;
        constexpr int kAlbedoTextureSlot = 2;
        constexpr int kBakedLightingTextureSlot = 3;
        constexpr int kDirectionalShadowCascadeTextureStartSlot = 4;
        constexpr int kShadowMap2DTextureSlot = kDirectionalShadowCascadeTextureStartSlot + scene::kMaxDirectionalShadowCascades;
        constexpr int kShadowMapCubeTextureSlot = kShadowMap2DTextureSlot + 1;
        constexpr int kLightPropagationVolumeTextureSlot = kShadowMapCubeTextureSlot + 1;
        constexpr int kPreviousLightPropagationVolumeTextureSlot = kLightPropagationVolumeTextureSlot + 1;
        constexpr int kBakedProbeTextureSlot = kPreviousLightPropagationVolumeTextureSlot + 1;
        constexpr int kAmbientPassMode = 0;
        constexpr int kLightPassMode = 1;
        constexpr int kIndirectTextureSlot = 0;
        constexpr int kAmbientOutputFull = 0;
        constexpr int kAmbientOutputLpvOnly = 1;
        constexpr int kAmbientOutputNone = 2;
        constexpr std::size_t kLightingSetupStage = 0;
        constexpr std::size_t kLightingAmbientStage = 1;
        constexpr std::size_t kLightingAccumulationStage = 2;

        struct IndirectLightingSettings
        {
            bool enableSsgi = true;
            IndirectDebugView debugView = IndirectDebugView::None;
        };

        bool IsLpvEffectEnabled(const RenderContext &ctx)
        {
            if (!ctx.postProcessEffects)
            {
                return false;
            }

            for (const auto *effect : *ctx.postProcessEffects)
            {
                if (effect && effect->IsEnabled() && effect->GetTypeName() == "LPV")
                {
                    return true;
                }
            }

            return false;
        }

        SSGIEffect *FindEnabledSsgiEffect(const RenderContext &ctx)
        {
            if (!ctx.postProcessEffects)
            {
                return nullptr;
            }

            for (auto *effect : *ctx.postProcessEffects)
            {
                if (!effect || !effect->IsEnabled() || effect->GetTypeName() != "SSGI")
                {
                    continue;
                }

                return static_cast<SSGIEffect *>(effect);
            }

            return nullptr;
        }

        RSMEffect *FindEnabledRsmEffect(const RenderContext &ctx)
        {
            if (!ctx.postProcessEffects)
            {
                return nullptr;
            }

            for (auto *effect : *ctx.postProcessEffects)
            {
                if (!effect || !effect->IsEnabled() || effect->GetTypeName() != "RSM")
                {
                    continue;
                }

                return static_cast<RSMEffect *>(effect);
            }

            return nullptr;
        }

        IndirectLightingSettings ResolveIndirectLightingSettings(const RenderContext &ctx)
        {
            IndirectLightingSettings settings;
            if (!ctx.postProcessEffects)
            {
                return settings;
            }

            for (auto *effect : *ctx.postProcessEffects)
            {
                if (!effect || !effect->IsEnabled() || effect->GetTypeName() != "SceneComposite")
                {
                    continue;
                }

                auto *sceneCompositeEffect = static_cast<SceneCompositeEffect *>(effect);
                settings.enableSsgi = sceneCompositeEffect->IsSsgiEnabled();
                settings.debugView = sceneCompositeEffect->GetIndirectDebugView();
                break;
            }

            return settings;
        }

        void BindLightingInputs(Shader *shader, const RenderContext &ctx)
        {
            auto *gBuffer = ctx.gBuffer;
            glActiveTexture(GL_TEXTURE0 + kPositionTextureSlot);
            glBindTexture(GL_TEXTURE_2D, gBuffer->GetPositionTextureID());
            shader->SetUniform("gPosition", kPositionTextureSlot);

            glActiveTexture(GL_TEXTURE0 + kNormalTextureSlot);
            glBindTexture(GL_TEXTURE_2D, gBuffer->GetNormalTextureID());
            shader->SetUniform("gNormal", kNormalTextureSlot);

            glActiveTexture(GL_TEXTURE0 + kAlbedoTextureSlot);
            glBindTexture(GL_TEXTURE_2D, gBuffer->GetAlbedoTextureID());
            shader->SetUniform("gAlbedoSpec", kAlbedoTextureSlot);

            glActiveTexture(GL_TEXTURE0 + kBakedLightingTextureSlot);
            glBindTexture(GL_TEXTURE_2D, gBuffer->GetBakedLightingTextureID());
            shader->SetUniform("gBakedLighting", kBakedLightingTextureSlot);

            for (int cascadeIndex = 0; cascadeIndex < scene::kMaxDirectionalShadowCascades; ++cascadeIndex)
            {
                const int textureSlot = kDirectionalShadowCascadeTextureStartSlot + cascadeIndex;
                glActiveTexture(GL_TEXTURE0 + textureSlot);
                glBindTexture(GL_TEXTURE_2D, 0);
                shader->SetUniform("uShadowCascadeMap" + std::to_string(cascadeIndex), textureSlot);
            }

            glActiveTexture(GL_TEXTURE0 + kShadowMap2DTextureSlot);
            glBindTexture(GL_TEXTURE_2D, 0);
            shader->SetUniform("uShadowMap2D", kShadowMap2DTextureSlot);

            glActiveTexture(GL_TEXTURE0 + kShadowMapCubeTextureSlot);
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
            shader->SetUniform("uShadowMapCube", kShadowMapCubeTextureSlot);

            auto *lpvPass = ctx.lightPropagationVolumePass;
            auto *lpvTexture = lpvPass ? lpvPass->GetVolumeTexture() : nullptr;
            glActiveTexture(GL_TEXTURE0 + kLightPropagationVolumeTextureSlot);
            glBindTexture(GL_TEXTURE_3D, lpvTexture ? lpvTexture->GetTextureID() : 0);
            shader->SetUniform("uLpvVolume", kLightPropagationVolumeTextureSlot);

            auto *previousLpvTexture = lpvPass ? lpvPass->GetPreviousVolumeTexture() : nullptr;
            glActiveTexture(GL_TEXTURE0 + kPreviousLightPropagationVolumeTextureSlot);
            glBindTexture(GL_TEXTURE_3D, previousLpvTexture ? previousLpvTexture->GetTextureID() : 0);
            shader->SetUniform("uPreviousLpvVolume", kPreviousLightPropagationVolumeTextureSlot);

            auto *bakedProbeTexture = ctx.scene ? ctx.scene->GetBakedProbeTexture() : nullptr;
            glActiveTexture(GL_TEXTURE0 + kBakedProbeTextureSlot);
            glBindTexture(GL_TEXTURE_3D, bakedProbeTexture ? bakedProbeTexture->GetTextureID() : 0);
            shader->SetUniform("uBakedProbeVolume", kBakedProbeTextureSlot);
        }

        bool BindShadowMapForLight(const scene::Light &light)
        {
            for (int cascadeIndex = 0; cascadeIndex < scene::kMaxDirectionalShadowCascades; ++cascadeIndex)
            {
                glActiveTexture(GL_TEXTURE0 + kDirectionalShadowCascadeTextureStartSlot + cascadeIndex);
                glBindTexture(GL_TEXTURE_2D, 0);
            }

            glActiveTexture(GL_TEXTURE0 + kShadowMap2DTextureSlot);
            glBindTexture(GL_TEXTURE_2D, 0);

            glActiveTexture(GL_TEXTURE0 + kShadowMapCubeTextureSlot);
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

            if (!light.castsShadows)
            {
                return false;
            }

            if (light.type == scene::LightType::Directional)
            {
                if (light.activeShadowCascadeCount <= 0)
                {
                    return false;
                }

                bool boundCascade = false;
                for (int cascadeIndex = 0; cascadeIndex < light.activeShadowCascadeCount; ++cascadeIndex)
                {
                    auto *shadowCascadeMap = light.shadowCascadeMaps[cascadeIndex].get();
                    if (!shadowCascadeMap)
                    {
                        continue;
                    }

                    glActiveTexture(GL_TEXTURE0 + kDirectionalShadowCascadeTextureStartSlot + cascadeIndex);
                    glBindTexture(GL_TEXTURE_2D, shadowCascadeMap->GetTextureID());
                    boundCascade = true;
                }

                return boundCascade;
            }

            if (!light.shadowMap)
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
            shader->SetUniform("uLight.IsStatic", light.isStatic ? 1 : 0);
            shader->SetUniform("uLight.CastsShadows", hasShadowMap ? 1 : 0);
            shader->SetUniform("uLight.LightSpaceMatrix", light.shadowMatrix);
            shader->SetUniform("uLight.ShadowFarPlane", light.shadowFarPlane);
            shader->SetUniform("uLight.CascadeCount", light.type == scene::LightType::Directional && hasShadowMap ? light.activeShadowCascadeCount : 0);
            shader->SetUniform("uLight.ShadowSoftness", light.directionalShadowSettings.softness);
            shader->SetUniform("uLight.CascadeBlendDistance", light.directionalShadowSettings.cascadeBlendDistance);

            for (int cascadeIndex = 0; cascadeIndex < scene::kMaxDirectionalShadowCascades; ++cascadeIndex)
            {
                shader->SetUniform("uLight.CascadeLightSpaceMatrices[" + std::to_string(cascadeIndex) + "]", light.shadowCascadeMatrices[cascadeIndex]);
                shader->SetUniform("uLight.CascadeSplits[" + std::to_string(cascadeIndex) + "]", light.shadowCascadeSplits[cascadeIndex]);
            }
        }
    }

    void LightingPass::Initialize()
    {
        m_lightingPassShader = Shader::CreateLightingPassShader();

        ShaderSource indirectCompositeSource;
        indirectCompositeSource.vertexSource = R"(
            #version 330 core

            out vec2 UV;

            void main()
            {
                vec2 vertices[3] = vec2[3](
                    vec2(-1.0, -1.0),
                    vec2(3.0, -1.0),
                    vec2(-1.0, 3.0)
                );
                gl_Position = vec4(vertices[gl_VertexID], 0.0, 1.0);
                UV = 0.5 * gl_Position.xy + vec2(0.5);
            }
        )";
        indirectCompositeSource.fragmentSource = R"(
            #version 330 core

            in vec2 UV;
            out vec4 FragColor;

            uniform sampler2D uIndirectTexture;

            void main()
            {
                FragColor = vec4(texture(uIndirectTexture, UV).rgb, 1.0);
            }
        )";

        m_indirectCompositeShader = Shader::Create(indirectCompositeSource);
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
                if (light->castsShadows && ((light->type == scene::LightType::Directional && light->activeShadowCascadeCount > 0) || light->shadowMap))
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
        BindLightingInputs(m_lightingPassShader, ctx);

        const glm::vec3 cameraPos = glm::vec3(glm::inverse(ctx.cameraData.view)[3]);
        auto *lpvPass = ctx.lightPropagationVolumePass;
        const IndirectLightingSettings indirectSettings = ResolveIndirectLightingSettings(ctx);
        const bool enableLpv = IsLpvEffectEnabled(ctx);
        const bool useRsm = FindEnabledRsmEffect(ctx) != nullptr;
        int ambientOutputMode = kAmbientOutputFull;
        bool renderDirectLighting = true;
        bool renderRsm = useRsm;
        bool renderSsgi = indirectSettings.enableSsgi;
        bool compositeRsmOnly = false;
        bool compositeSsgiOnly = false;

        const auto compositeIndirectTarget = [&](RenderTarget *resolvedIndirectTarget, bool indirectOnly)
        {
            if (!resolvedIndirectTarget)
            {
                return;
            }

            Graphics::BindRenderTarget(ctx.temporaryRenderTarget);
            glViewport(0, 0, ctx.temporaryRenderTarget->GetWidth(), ctx.temporaryRenderTarget->GetHeight());
            if (indirectOnly)
            {
                glDisable(GL_BLEND);
            }
            else
            {
                glEnable(GL_BLEND);
                glBlendEquation(GL_FUNC_ADD);
                glBlendFunc(GL_ONE, GL_ONE);
            }

            m_indirectCompositeShader->Bind();
            glActiveTexture(GL_TEXTURE0 + kIndirectTextureSlot);
            glBindTexture(GL_TEXTURE_2D, resolvedIndirectTarget->GetColorTextureID());
            m_indirectCompositeShader->SetUniform("uIndirectTexture", kIndirectTextureSlot);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        };

        switch (indirectSettings.debugView)
        {
        case IndirectDebugView::GiOnly:
            ambientOutputMode = enableLpv ? kAmbientOutputLpvOnly : kAmbientOutputNone;
            renderDirectLighting = false;
            renderRsm = useRsm;
            renderSsgi = false;
            compositeRsmOnly = useRsm;
            break;
        case IndirectDebugView::RsmOnly:
            ambientOutputMode = kAmbientOutputNone;
            renderDirectLighting = false;
            renderRsm = useRsm;
            renderSsgi = false;
            compositeRsmOnly = true;
            break;
        case IndirectDebugView::SsgiOnly:
            ambientOutputMode = kAmbientOutputNone;
            renderDirectLighting = false;
            renderRsm = false;
            renderSsgi = indirectSettings.enableSsgi;
            compositeSsgiOnly = true;
            break;
        case IndirectDebugView::CombinedIndirect:
            ambientOutputMode = enableLpv ? kAmbientOutputLpvOnly : kAmbientOutputNone;
            renderDirectLighting = false;
            renderRsm = useRsm;
            renderSsgi = indirectSettings.enableSsgi;
            break;
        case IndirectDebugView::None:
        default:
            renderRsm = useRsm;
            break;
        }

        m_lightingPassShader->SetUniform("uViewPos", cameraPos);
        m_lightingPassShader->SetUniform("uViewMatrix", ctx.cameraData.view);
        m_lightingPassShader->SetUniform("uLpvEnabled", enableLpv && lpvPass && lpvPass->GetVolumeTexture() ? 1 : 0);
        m_lightingPassShader->SetUniform("uLpvOrigin", lpvPass ? lpvPass->GetGridOrigin() : glm::vec3(0.0f));
        m_lightingPassShader->SetUniform("uLpvSize", lpvPass ? lpvPass->GetGridSize() : glm::vec3(1.0f));
        m_lightingPassShader->SetUniform("uPreviousLpvOrigin", lpvPass ? lpvPass->GetPreviousGridOrigin() : glm::vec3(0.0f));
        m_lightingPassShader->SetUniform("uPreviousLpvSize", lpvPass ? lpvPass->GetPreviousGridSize() : glm::vec3(1.0f));
        m_lightingPassShader->SetUniform("uLpvTransitionBlend", lpvPass ? lpvPass->GetTransitionBlendFactor() : 1.0f);
        m_lightingPassShader->SetUniform("uAmbientOutputMode", ambientOutputMode);
        m_lightingPassShader->SetUniform("uBakedProbeEnabled", ctx.scene && ctx.scene->HasBakedProbeVolume() ? 1 : 0);
        m_lightingPassShader->SetUniform("uBakedProbeOrigin", ctx.scene ? ctx.scene->GetBakedProbeVolume().origin : glm::vec3(0.0f));
        m_lightingPassShader->SetUniform("uBakedProbeSize", ctx.scene ? ctx.scene->GetBakedProbeVolume().size : glm::vec3(1.0f));

        glDisable(GL_BLEND);
        m_lightingPassShader->SetUniform("uPassMode", kAmbientPassMode);
        glBindVertexArray(0);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        if (ctx.renderer)
        {
            ctx.renderer->EndLightingStageTiming(kLightingAmbientStage);
            ctx.renderer->BeginLightingStageTiming(kLightingAccumulationStage);
        }

        if (renderDirectLighting)
        {
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
        }

        if (m_indirectCompositeShader && renderSsgi)
        {
            if (auto *ssgiEffect = FindEnabledSsgiEffect(ctx))
            {
                RenderTarget *resolvedIndirectTarget = ssgiEffect->GenerateResolvedIndirectLighting(PostProcessContext{
                                                                                                        .renderContext = ctx,
                                                                                                        .sourceRenderTarget = ctx.temporaryRenderTarget,
                                                                                                        .destinationRenderTarget = nullptr,
                                                                                                    },
                                                                                                    ctx.temporaryRenderTarget->GetWidth(), ctx.temporaryRenderTarget->GetHeight());
                compositeIndirectTarget(resolvedIndirectTarget, compositeSsgiOnly || ssgiEffect->OutputsIndirectOnly());
            }
        }

        if (m_indirectCompositeShader && renderRsm)
        {
            if (auto *rsmEffect = FindEnabledRsmEffect(ctx))
            {
                RenderTarget *resolvedIndirectTarget = rsmEffect->GenerateResolvedIndirectLighting(PostProcessContext{
                                                                                                       .renderContext = ctx,
                                                                                                       .sourceRenderTarget = ctx.temporaryRenderTarget,
                                                                                                       .destinationRenderTarget = nullptr,
                                                                                                   },
                                                                                                   ctx.temporaryRenderTarget->GetWidth(), ctx.temporaryRenderTarget->GetHeight());
                compositeIndirectTarget(resolvedIndirectTarget, compositeRsmOnly);
            }
        }

        glDisable(GL_BLEND);
        if (ctx.renderer)
        {
            ctx.renderer->EndLightingStageTiming(kLightingAccumulationStage);
        }
        Graphics::UnbindRenderTarget();
    }
}