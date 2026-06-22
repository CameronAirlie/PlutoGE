#include "PlutoGE/render/passes/TransparentPass.h"

#include "PlutoGE/render/GBuffer.h"
#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace PlutoGE::render
{
    namespace
    {
        struct TransparentInstanceData
        {
            glm::mat4 model{1.0f};
        };

        constexpr int kTransparentEnvironmentTextureSlot = 8;
        constexpr int kTransparentIblCaptureTextureSlotStart = 9;
        constexpr int kTransparentSceneColorTextureSlot = 13;
        constexpr int kTransparentShadowTextureSlotStart = 14;
        constexpr int kMaxTransparentLights = 16;
        constexpr int kMaxTransparentShadowMaps = 16;
        constexpr int kMaxTransparentShadowCascades = scene::kMaxDirectionalShadowCascades;

        bool IsTransparentCommand(const RenderCommand &command)
        {
            return command.material &&
                   command.mesh &&
                   command.material->GetConfig().alphaMode == AlphaMode::Blend;
        }

        void ConfigureMatrixAttributes(unsigned int baseLocation, std::size_t offset, std::size_t stride)
        {
            for (unsigned int column = 0; column < 4; ++column)
            {
                const unsigned int location = baseLocation + column;
                glEnableVertexAttribArray(location);
                glVertexAttribPointer(location, 4, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride), reinterpret_cast<const void *>(offset + sizeof(glm::vec4) * column));
                glVertexAttribDivisor(location, 1);
            }
        }

        void BindTransparentInstanceAttributes(const Mesh &mesh, unsigned int instanceBuffer)
        {
            glBindVertexArray(mesh.GetVAO());
            glBindBuffer(GL_ARRAY_BUFFER, instanceBuffer);
            ConfigureMatrixAttributes(5, offsetof(TransparentInstanceData, model), sizeof(TransparentInstanceData));
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }

        void UploadTransparentInstances(unsigned int &instanceBuffer,
                                        std::size_t &instanceCapacity,
                                        const std::vector<TransparentInstanceData> &instances)
        {
            if (instances.empty())
            {
                return;
            }

            if (instanceBuffer == 0)
            {
                glGenBuffers(1, &instanceBuffer);
            }

            glBindBuffer(GL_ARRAY_BUFFER, instanceBuffer);
            if (instanceCapacity < instances.size())
            {
                instanceCapacity = std::max(instances.size(), instanceCapacity == 0 ? instances.size() : instanceCapacity * 2);
            }

            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(instanceCapacity * sizeof(TransparentInstanceData)), nullptr, GL_STREAM_DRAW);
            glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(instances.size() * sizeof(TransparentInstanceData)), instances.data());
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }

        void UploadJointMatrices(Shader *shader, const std::vector<glm::mat4> *jointMatrices)
        {
            constexpr size_t kMaxShaderJoints = 48;
            if (!shader || !jointMatrices || jointMatrices->empty())
            {
                shader->SetUniform("uUseSkinning", 0);
                return;
            }

            shader->SetUniform("uUseSkinning", 1);
            const size_t jointCount = std::min(jointMatrices->size(), kMaxShaderJoints);
            for (size_t jointIndex = 0; jointIndex < jointCount; ++jointIndex)
            {
                shader->SetUniform(std::string("uJointMatrices[") + std::to_string(jointIndex) + "]", (*jointMatrices)[jointIndex]);
            }
        }

        float ResolveMaxMipLevel(const Texture *texture)
        {
            return texture ? static_cast<float>(std::max(0, static_cast<int>(std::floor(std::log2(static_cast<float>(std::max(texture->GetWidth(), texture->GetHeight()))))))) : 0.0f;
        }

        float ResolveMaxMipLevel(int width, int height)
        {
            return static_cast<float>(std::max(0, static_cast<int>(std::floor(std::log2(static_cast<float>(std::max(width, height)))))));
        }

        void BindTransparentEnvironment(Shader *shader, const RenderContext &ctx)
        {
            const auto *environmentTexture = ctx.scene ? ctx.scene->GetEnvironmentMapTexture() : nullptr;
            glActiveTexture(GL_TEXTURE0 + kTransparentEnvironmentTextureSlot);
            glBindTexture(GL_TEXTURE_2D, environmentTexture ? environmentTexture->GetTextureID() : 0);
            shader->SetUniform("uEnvironmentMap", kTransparentEnvironmentTextureSlot);
            shader->SetUniform("uEnvironmentEnabled", environmentTexture ? 1 : 0);
            shader->SetUniform("uEnvironmentIntensity", ctx.scene ? ctx.scene->GetEnvironmentIntensity() : 1.0f);

            float reflectionMaxMipLevel = ResolveMaxMipLevel(environmentTexture);
            const auto &iblCaptureVolumes = ctx.scene ? ctx.scene->GetIblCaptureVolumes() : std::vector<scene::IblCaptureVolume>{};
            const int iblCaptureCount = std::min(scene::kMaxIblCaptureVolumes, static_cast<int>(iblCaptureVolumes.size()));
            for (int captureIndex = 0; captureIndex < scene::kMaxIblCaptureVolumes; ++captureIndex)
            {
                const bool hasCapture = captureIndex < static_cast<int>(iblCaptureVolumes.size()) &&
                                        iblCaptureVolumes[static_cast<std::size_t>(captureIndex)].IsValid() &&
                                        iblCaptureVolumes[static_cast<std::size_t>(captureIndex)].environmentMapTexture->GetType() == GL_TEXTURE_CUBE_MAP;
                const auto &captureVolume = hasCapture ? iblCaptureVolumes[static_cast<std::size_t>(captureIndex)] : scene::IblCaptureVolume{};
                const int textureSlot = kTransparentIblCaptureTextureSlotStart + captureIndex;
                glActiveTexture(GL_TEXTURE0 + textureSlot);
                glBindTexture(GL_TEXTURE_CUBE_MAP, hasCapture ? captureVolume.environmentMapTexture->GetTextureID() : 0);
                shader->SetUniform("uIblCaptureMaps[" + std::to_string(captureIndex) + "]", textureSlot);
                shader->SetUniform("uIblCaptureOrigins[" + std::to_string(captureIndex) + "]", captureVolume.origin);
                shader->SetUniform("uIblCaptureSizes[" + std::to_string(captureIndex) + "]", captureVolume.size);
                shader->SetUniform("uIblCaptureIntensities[" + std::to_string(captureIndex) + "]", hasCapture ? captureVolume.intensity : 0.0f);
                shader->SetUniform("uIblCaptureBlendDistances[" + std::to_string(captureIndex) + "]", captureVolume.blendDistance);
                const float captureMaxMipLevel = hasCapture ? ResolveMaxMipLevel(captureVolume.environmentMapTexture) : 0.0f;
                reflectionMaxMipLevel = std::max(reflectionMaxMipLevel, captureMaxMipLevel);
                shader->SetUniform("uIblCaptureMaxMipLevels[" + std::to_string(captureIndex) + "]", captureMaxMipLevel);
                shader->SetUniform("uIblCaptureEnabled[" + std::to_string(captureIndex) + "]", hasCapture ? 1 : 0);
            }
            shader->SetUniform("uEnvironmentMaxMipLevel", reflectionMaxMipLevel);
            shader->SetUniform("uIblCaptureCount", iblCaptureCount);
        }

        void BindTransparentLights(Shader *shader, const RenderContext &ctx)
        {
            const int lightCount = std::min(kMaxTransparentLights, ctx.lights ? static_cast<int>(ctx.lights->size()) : 0);
            int shadowMapCount = 0;
            shader->SetUniform("uLightCount", lightCount);
            for (int lightIndex = 0; lightIndex < lightCount; ++lightIndex)
            {
                const auto *light = (*ctx.lights)[static_cast<std::size_t>(lightIndex)];
                if (!light)
                {
                    continue;
                }

                const std::string index = "[" + std::to_string(lightIndex) + "]";
                shader->SetUniform("uLightPositions" + index, light->position);
                shader->SetUniform("uLightColors" + index, light->color);
                shader->SetUniform("uLightIntensities" + index, light->intensity);
                shader->SetUniform("uLightRanges" + index, light->range);
                shader->SetUniform("uLightDirections" + index, light->direction);
                shader->SetUniform("uLightTypes" + index, static_cast<int>(light->type));
                shader->SetUniform("uLightCastsShadows" + index, 0);
                shader->SetUniform("uLightShadowMapBase" + index, -1);
                shader->SetUniform("uLightCascadeCount" + index, 0);
                shader->SetUniform("uLightCascadeSplits" + index, glm::vec4(0.0f));
                shader->SetUniform("uLightCascadeBlendDistances" + index, light->directionalShadowSettings.cascadeBlendDistance);

                const bool hasDirectionalShadows =
                    light->type == scene::LightType::Directional &&
                    light->castsShadows &&
                    light->activeShadowCascadeCount > 0;
                if (!hasDirectionalShadows)
                {
                    continue;
                }

                const int cascadeCount = std::min(kMaxTransparentShadowCascades, light->activeShadowCascadeCount);
                if (shadowMapCount + cascadeCount > kMaxTransparentShadowMaps)
                {
                    continue;
                }

                bool hasAllCascadeMaps = true;
                for (int cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex)
                {
                    if (!light->shadowCascadeMaps[cascadeIndex])
                    {
                        hasAllCascadeMaps = false;
                        break;
                    }
                }
                if (!hasAllCascadeMaps)
                {
                    continue;
                }

                const int shadowMapBase = shadowMapCount;
                shader->SetUniform("uLightCastsShadows" + index, 1);
                shader->SetUniform("uLightShadowMapBase" + index, shadowMapBase);
                shader->SetUniform("uLightCascadeCount" + index, cascadeCount);
                shader->SetUniform("uLightCascadeSplits" + index, glm::vec4(
                                                                      light->shadowCascadeSplits[0],
                                                                      light->shadowCascadeSplits[1],
                                                                      light->shadowCascadeSplits[2],
                                                                      light->shadowCascadeSplits[3]));
                for (int cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex)
                {
                    const int shadowMapIndex = shadowMapBase + cascadeIndex;
                    const int textureSlot = kTransparentShadowTextureSlotStart + shadowMapIndex;
                    glActiveTexture(GL_TEXTURE0 + textureSlot);
                    glBindTexture(GL_TEXTURE_2D, light->shadowCascadeMaps[cascadeIndex]->GetTextureID());
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

                    shader->SetUniform("uDirectionalShadowMaps[" + std::to_string(shadowMapIndex) + "]", textureSlot);
                    const int cascadeUniformIndex = lightIndex * kMaxTransparentShadowCascades + cascadeIndex;
                    shader->SetUniform("uLightCascadeMatrices[" + std::to_string(cascadeUniformIndex) + "]", light->shadowCascadeMatrices[cascadeIndex]);
                    shader->SetUniform("uLightCascadeOrigins[" + std::to_string(cascadeUniformIndex) + "]", light->shadowCascadeWorldOrigins[cascadeIndex]);
                }

                shadowMapCount += cascadeCount;
            }
        }

        void CopySceneColorForTransparentRefraction(RenderTarget &source, RenderTarget &destination)
        {
            destination.Resize(source.GetWidth(), source.GetHeight());

            glBindFramebuffer(GL_READ_FRAMEBUFFER, source.GetFramebufferID());
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination.GetFramebufferID());
            glBlitFramebuffer(
                0, 0, source.GetWidth(), source.GetHeight(),
                0, 0, destination.GetWidth(), destination.GetHeight(),
                GL_COLOR_BUFFER_BIT,
                GL_LINEAR);

            glBindTexture(GL_TEXTURE_2D, destination.GetColorTextureID());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glGenerateMipmap(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        void CopyFramebufferColorForTransparentRefraction(GLuint sourceFramebuffer, int width, int height, RenderTarget &destination)
        {
            destination.Resize(width, height);

            glBindFramebuffer(GL_READ_FRAMEBUFFER, sourceFramebuffer);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination.GetFramebufferID());
            glBlitFramebuffer(
                0, 0, width, height,
                0, 0, destination.GetWidth(), destination.GetHeight(),
                GL_COLOR_BUFFER_BIT,
                GL_LINEAR);

            glBindTexture(GL_TEXTURE_2D, destination.GetColorTextureID());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glGenerateMipmap(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }

    void TransparentPass::Initialize()
    {
        m_transparentShader = Shader::CreateTransparentPassShader();
        if (m_instanceBuffer == 0)
        {
            glGenBuffers(1, &m_instanceBuffer);
        }
    }

    void TransparentPass::Execute(const RenderContext &ctx)
    {
        if (!ctx.renderCommands || !ctx.temporaryRenderTarget || !ctx.gBuffer || !ctx.gBuffer->IsInitialized())
        {
            return;
        }

        const int targetWidth = ctx.renderTarget ? ctx.renderTarget->GetWidth() : ctx.temporaryRenderTarget->GetWidth();
        const int targetHeight = ctx.renderTarget ? ctx.renderTarget->GetHeight() : ctx.temporaryRenderTarget->GetHeight();
        const GLuint targetFramebuffer = ctx.renderTarget ? ctx.renderTarget->GetFramebufferID() : 0;

        std::vector<const RenderCommand *> transparentCommands;
        transparentCommands.reserve(ctx.renderCommands->size());
        for (const auto &command : *ctx.renderCommands)
        {
            if (IsTransparentCommand(command))
            {
                transparentCommands.push_back(&command);
            }
        }

        if (transparentCommands.empty())
        {
            return;
        }

        if (!m_sceneColorCopy)
        {
            m_sceneColorCopy = std::make_unique<RenderTarget>(RenderTargetConfig{
                .width = targetWidth,
                .height = targetHeight,
                .clearColor = glm::vec4(0.0f),
            });
        }
        if (ctx.renderTarget)
        {
            CopySceneColorForTransparentRefraction(*ctx.renderTarget, *m_sceneColorCopy);
        }
        else
        {
            CopyFramebufferColorForTransparentRefraction(targetFramebuffer, targetWidth, targetHeight, *m_sceneColorCopy);
        }

        const glm::vec3 cameraPosition = glm::vec3(glm::inverse(ctx.cameraData.view)[3]);
        std::sort(transparentCommands.begin(), transparentCommands.end(), [&](const RenderCommand *a, const RenderCommand *b)
                  {
                      const glm::vec3 offsetA = a->worldBounds.center - cameraPosition;
                      const glm::vec3 offsetB = b->worldBounds.center - cameraPosition;
                      const float distanceA = glm::dot(offsetA, offsetA);
                      const float distanceB = glm::dot(offsetB, offsetB);
                      return distanceA > distanceB;
                  });

        glBindFramebuffer(GL_READ_FRAMEBUFFER, ctx.gBuffer->GetFBO());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, targetFramebuffer);
        glBlitFramebuffer(
            0, 0, ctx.gBuffer->GetWidth(), ctx.gBuffer->GetHeight(),
            0, 0, targetWidth, targetHeight,
            GL_DEPTH_BUFFER_BIT,
            GL_NEAREST);

        if (ctx.renderTarget)
        {
            Graphics::BindRenderTarget(ctx.renderTarget);
        }
        else
        {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, targetWidth, targetHeight);
        }
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        m_transparentShader->Bind();
        m_transparentShader->SetUniform("uView", ctx.cameraData.view);
        m_transparentShader->SetUniform("uProjection", ctx.cameraData.projection);
        m_transparentShader->SetUniform("uViewPos", cameraPosition);
        glActiveTexture(GL_TEXTURE0 + kTransparentSceneColorTextureSlot);
        glBindTexture(GL_TEXTURE_2D, m_sceneColorCopy ? m_sceneColorCopy->GetColorTextureID() : 0);
        m_transparentShader->SetUniform("uSceneColorTexture", kTransparentSceneColorTextureSlot);
        m_transparentShader->SetUniform("uSceneColorEnabled", m_sceneColorCopy && m_sceneColorCopy->IsInitialized() ? 1 : 0);
        m_transparentShader->SetUniform("uSceneColorTextureSize", glm::vec2(
                                                                  static_cast<float>(m_sceneColorCopy ? m_sceneColorCopy->GetWidth() : 1),
                                                                  static_cast<float>(m_sceneColorCopy ? m_sceneColorCopy->GetHeight() : 1)));
        m_transparentShader->SetUniform("uSceneColorMaxMipLevel", m_sceneColorCopy ? ResolveMaxMipLevel(m_sceneColorCopy->GetWidth(), m_sceneColorCopy->GetHeight()) : 0.0f);
        BindTransparentEnvironment(m_transparentShader, ctx);
        BindTransparentLights(m_transparentShader, ctx);

        for (const auto *command : transparentCommands)
        {
            command->material->Bind(m_transparentShader);
            UploadJointMatrices(m_transparentShader, command->jointMatrices);
            const std::vector<TransparentInstanceData> instance{
                TransparentInstanceData{.model = command->model},
            };
            UploadTransparentInstances(m_instanceBuffer, m_instanceCapacity, instance);
            BindTransparentInstanceAttributes(*command->mesh, m_instanceBuffer);
            command->mesh->DrawSubmeshInstancedBound(command->submeshIndex, 1, command->lodIndex);
        }

        m_transparentShader->SetUniform("uUseSkinning", 0);
        m_transparentShader->Unbind();
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glDepthFunc(GL_LESS);
        Graphics::UnbindRenderTarget();
    }
}
