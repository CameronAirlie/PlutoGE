#include "PlutoGE/render/passes/TransparentPass.h"

#include "PlutoGE/render/GBuffer.h"
#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/UniformNames.h"
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

        bool CanBatchTransparentCommands(const RenderCommand &a, const RenderCommand &b)
        {
            if (a.jointMatrices || b.jointMatrices)
            {
                return false;
            }

            return a.material == b.material &&
                   a.mesh == b.mesh &&
                   a.mesh->GetSubmeshLodRange(a.submeshIndex, a.lodIndex).indexOffset == b.mesh->GetSubmeshLodRange(b.submeshIndex, b.lodIndex).indexOffset &&
                   a.mesh->GetSubmeshLodRange(a.submeshIndex, a.lodIndex).indexCount == b.mesh->GetSubmeshLodRange(b.submeshIndex, b.lodIndex).indexCount;
        }

        void AppendTransparentInstances(const RenderCommand &command, std::vector<TransparentInstanceData> &instances)
        {
            if (!command.instanceModels || command.instanceModels->empty())
            {
                instances.push_back(TransparentInstanceData{.model = command.model});
                return;
            }

            instances.reserve(instances.size() + command.instanceModels->size());
            for (const auto &model : *command.instanceModels)
                instances.push_back(TransparentInstanceData{.model = model});
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
            constexpr size_t kMaxShaderJoints = 128;
            if (!shader || !jointMatrices || jointMatrices->empty())
            {
                if (shader)
                    shader->SetUniform("uUseSkinning", 0);
                return;
            }

            shader->SetUniform("uUseSkinning", 1);
            const size_t jointCount = std::min(jointMatrices->size(), kMaxShaderJoints);
            shader->SetUniformMatrixArray("uJointMatrices[0]", jointMatrices->data(), jointCount);
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
            static const auto iblMapNames = MakeArrayUniformNames<scene::kMaxIblCaptureVolumes>("uIblCaptureMaps");
            static const auto iblOriginNames = MakeArrayUniformNames<scene::kMaxIblCaptureVolumes>("uIblCaptureOrigins");
            static const auto iblSizeNames = MakeArrayUniformNames<scene::kMaxIblCaptureVolumes>("uIblCaptureSizes");
            static const auto iblIntensityNames = MakeArrayUniformNames<scene::kMaxIblCaptureVolumes>("uIblCaptureIntensities");
            static const auto iblBlendNames = MakeArrayUniformNames<scene::kMaxIblCaptureVolumes>("uIblCaptureBlendDistances");
            static const auto iblMipNames = MakeArrayUniformNames<scene::kMaxIblCaptureVolumes>("uIblCaptureMaxMipLevels");
            static const auto iblEnabledNames = MakeArrayUniformNames<scene::kMaxIblCaptureVolumes>("uIblCaptureEnabled");
            const auto *environmentTexture = ctx.scene ? ctx.scene->GetEnvironmentMapTexture() : nullptr;
            const GLuint physicalSkyTexture = ctx.renderer ? ctx.renderer->GetPhysicalSkyEnvironmentTextureID() : 0;
            Graphics::ActiveTexture(GL_TEXTURE0 + kTransparentEnvironmentTextureSlot);
            Graphics::BindTexture(GL_TEXTURE_2D, physicalSkyTexture ? physicalSkyTexture : (environmentTexture ? environmentTexture->GetTextureID() : 0));
            shader->SetUniform("uEnvironmentMap", kTransparentEnvironmentTextureSlot);
            shader->SetUniform("uEnvironmentEnabled", physicalSkyTexture || environmentTexture ? 1 : 0);
            shader->SetUniform("uEnvironmentIntensity", ctx.scene ? ctx.scene->GetEnvironmentIntensity() : 1.0f);

            float reflectionMaxMipLevel = physicalSkyTexture
                                              ? ResolveMaxMipLevel(ctx.renderer->GetPhysicalSkyEnvironmentWidth(), ctx.renderer->GetPhysicalSkyEnvironmentHeight())
                                              : ResolveMaxMipLevel(environmentTexture);
            const auto &iblCaptureVolumes = ctx.scene ? ctx.scene->GetIblCaptureVolumes() : std::vector<scene::IblCaptureVolume>{};
            int iblCaptureCount = 0;
            for (int captureIndex = 0; captureIndex < scene::kMaxIblCaptureVolumes; ++captureIndex)
            {
                const bool hasCapture = captureIndex < static_cast<int>(iblCaptureVolumes.size()) &&
                                        iblCaptureVolumes[static_cast<std::size_t>(captureIndex)].IsValid() &&
                                        iblCaptureVolumes[static_cast<std::size_t>(captureIndex)].environmentMapTexture->GetType() == GL_TEXTURE_CUBE_MAP;
                if (hasCapture)
                {
                    iblCaptureCount = captureIndex + 1;
                }
                const auto &captureVolume = hasCapture ? iblCaptureVolumes[static_cast<std::size_t>(captureIndex)] : scene::IblCaptureVolume{};
                const int textureSlot = kTransparentIblCaptureTextureSlotStart + captureIndex;
                Graphics::ActiveTexture(GL_TEXTURE0 + textureSlot);
                Graphics::BindTexture(GL_TEXTURE_CUBE_MAP, hasCapture ? captureVolume.environmentMapTexture->GetTextureID() : 0);
                shader->SetUniform(iblMapNames[captureIndex], textureSlot);
                shader->SetUniform(iblOriginNames[captureIndex], captureVolume.origin);
                shader->SetUniform(iblSizeNames[captureIndex], captureVolume.size);
                shader->SetUniform(iblIntensityNames[captureIndex], hasCapture ? captureVolume.intensity : 0.0f);
                shader->SetUniform(iblBlendNames[captureIndex], captureVolume.blendDistance);
                const float captureMaxMipLevel = hasCapture ? ResolveMaxMipLevel(captureVolume.environmentMapTexture) : 0.0f;
                reflectionMaxMipLevel = std::max(reflectionMaxMipLevel, captureMaxMipLevel);
                shader->SetUniform(iblMipNames[captureIndex], captureMaxMipLevel);
                shader->SetUniform(iblEnabledNames[captureIndex], hasCapture ? 1 : 0);
            }
            shader->SetUniform("uEnvironmentMaxMipLevel", reflectionMaxMipLevel);
            shader->SetUniform("uIblCaptureCount", iblCaptureCount);
        }

        void BindTransparentLights(Shader *shader, const RenderContext &ctx)
        {
            static const auto lightPositionNames = MakeArrayUniformNames<kMaxTransparentLights>("uLightPositions");
            static const auto lightColorNames = MakeArrayUniformNames<kMaxTransparentLights>("uLightColors");
            static const auto lightIntensityNames = MakeArrayUniformNames<kMaxTransparentLights>("uLightIntensities");
            static const auto lightRangeNames = MakeArrayUniformNames<kMaxTransparentLights>("uLightRanges");
            static const auto lightDirectionNames = MakeArrayUniformNames<kMaxTransparentLights>("uLightDirections");
            static const auto lightTypeNames = MakeArrayUniformNames<kMaxTransparentLights>("uLightTypes");
            static const auto lightCastsShadowNames = MakeArrayUniformNames<kMaxTransparentLights>("uLightCastsShadows");
            static const auto lightShadowBaseNames = MakeArrayUniformNames<kMaxTransparentLights>("uLightShadowMapBase");
            static const auto lightCascadeCountNames = MakeArrayUniformNames<kMaxTransparentLights>("uLightCascadeCount");
            static const auto lightCascadeSplitNames = MakeArrayUniformNames<kMaxTransparentLights>("uLightCascadeSplits");
            static const auto lightCascadeBlendNames = MakeArrayUniformNames<kMaxTransparentLights>("uLightCascadeBlendDistances");
            static const auto directionalShadowMapNames = MakeArrayUniformNames<kMaxTransparentShadowMaps>("uDirectionalShadowMaps");
            static const auto cascadeMatrixNames = MakeArrayUniformNames<kMaxTransparentLights * kMaxTransparentShadowCascades>("uLightCascadeMatrices");
            static const auto cascadeOriginNames = MakeArrayUniformNames<kMaxTransparentLights * kMaxTransparentShadowCascades>("uLightCascadeOrigins");
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

                shader->SetUniform(lightPositionNames[lightIndex], light->position);
                shader->SetUniform(lightColorNames[lightIndex], light->color);
                const float skyVisibility = ctx.renderer ? ctx.renderer->GetPhysicalSkyDirectionalLightVisibility(light) : 1.0f;
                shader->SetUniform(lightIntensityNames[lightIndex], light->intensity * skyVisibility);
                shader->SetUniform(lightRangeNames[lightIndex], light->range);
                shader->SetUniform(lightDirectionNames[lightIndex], light->direction);
                shader->SetUniform(lightTypeNames[lightIndex], static_cast<int>(light->type));
                shader->SetUniform(lightCastsShadowNames[lightIndex], 0);
                shader->SetUniform(lightShadowBaseNames[lightIndex], -1);
                shader->SetUniform(lightCascadeCountNames[lightIndex], 0);
                shader->SetUniform(lightCascadeSplitNames[lightIndex], glm::vec4(0.0f));
                shader->SetUniform(lightCascadeBlendNames[lightIndex], light->directionalShadowSettings.cascadeBlendDistance);

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
                shader->SetUniform(lightCastsShadowNames[lightIndex], 1);
                shader->SetUniform(lightShadowBaseNames[lightIndex], shadowMapBase);
                shader->SetUniform(lightCascadeCountNames[lightIndex], cascadeCount);
                shader->SetUniform(lightCascadeSplitNames[lightIndex], glm::vec4(
                                                                           light->shadowCascadeSplits[0],
                                                                           light->shadowCascadeSplits[1],
                                                                           light->shadowCascadeSplits[2],
                                                                           light->shadowCascadeSplits[3]));
                for (int cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex)
                {
                    const int shadowMapIndex = shadowMapBase + cascadeIndex;
                    const int textureSlot = kTransparentShadowTextureSlotStart + shadowMapIndex;
                    Graphics::ActiveTexture(GL_TEXTURE0 + textureSlot);
                    Graphics::BindTexture(GL_TEXTURE_2D, light->shadowCascadeMaps[cascadeIndex]->GetTextureID());

                    shader->SetUniform(directionalShadowMapNames[shadowMapIndex], textureSlot);
                    const int cascadeUniformIndex = lightIndex * kMaxTransparentShadowCascades + cascadeIndex;
                    shader->SetUniform(cascadeMatrixNames[cascadeUniformIndex], light->shadowCascadeMatrices[cascadeIndex]);
                    shader->SetUniform(cascadeOriginNames[cascadeUniformIndex], light->shadowCascadeWorldOrigins[cascadeIndex]);
                }

                shadowMapCount += cascadeCount;
            }
        }

        void CopySceneColorForTransparentRefraction(RenderTarget &source, RenderTarget &destination)
        {
            destination.Resize(source.GetWidth(), source.GetHeight());

            Graphics::BindFramebuffer(GL_READ_FRAMEBUFFER, source.GetFramebufferID());
            Graphics::BindFramebuffer(GL_DRAW_FRAMEBUFFER, destination.GetFramebufferID());
            glBlitFramebuffer(
                0, 0, source.GetWidth(), source.GetHeight(),
                0, 0, destination.GetWidth(), destination.GetHeight(),
                GL_COLOR_BUFFER_BIT,
                GL_LINEAR);

            Graphics::BindTexture(GL_TEXTURE_2D, destination.GetColorTextureID());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glGenerateMipmap(GL_TEXTURE_2D);
            Graphics::BindTexture(GL_TEXTURE_2D, 0);
        }

        void CopyFramebufferColorForTransparentRefraction(GLuint sourceFramebuffer, int width, int height, RenderTarget &destination)
        {
            destination.Resize(width, height);

            Graphics::BindFramebuffer(GL_READ_FRAMEBUFFER, sourceFramebuffer);
            Graphics::BindFramebuffer(GL_DRAW_FRAMEBUFFER, destination.GetFramebufferID());
            glBlitFramebuffer(
                0, 0, width, height,
                0, 0, destination.GetWidth(), destination.GetHeight(),
                GL_COLOR_BUFFER_BIT,
                GL_LINEAR);

            Graphics::BindTexture(GL_TEXTURE_2D, destination.GetColorTextureID());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glGenerateMipmap(GL_TEXTURE_2D);
            Graphics::BindTexture(GL_TEXTURE_2D, 0);
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
                      return distanceA > distanceB; });

        Graphics::BindFramebuffer(GL_READ_FRAMEBUFFER, ctx.gBuffer->GetFBO());
        Graphics::BindFramebuffer(GL_DRAW_FRAMEBUFFER, targetFramebuffer);
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
            Graphics::BindFramebuffer(GL_FRAMEBUFFER, 0);
            Graphics::SetViewport(0, 0, targetWidth, targetHeight);
        }
        Graphics::Enable(GL_DEPTH_TEST);
        glDepthFunc(GL_GEQUAL);
        glDepthMask(GL_FALSE);
        Graphics::Disable(GL_CULL_FACE);
        Graphics::Enable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        m_transparentShader->Bind();
        m_transparentShader->SetUniform("uView", ctx.cameraData.view);
        m_transparentShader->SetUniform("uProjection", ctx.cameraData.projection);
        m_transparentShader->SetUniform("uViewPos", cameraPosition);
        Graphics::ActiveTexture(GL_TEXTURE0 + kTransparentSceneColorTextureSlot);
        Graphics::BindTexture(GL_TEXTURE_2D, m_sceneColorCopy ? m_sceneColorCopy->GetColorTextureID() : 0);
        m_transparentShader->SetUniform("uSceneColorTexture", kTransparentSceneColorTextureSlot);
        m_transparentShader->SetUniform("uSceneColorEnabled", m_sceneColorCopy && m_sceneColorCopy->IsInitialized() ? 1 : 0);
        m_transparentShader->SetUniform("uSceneColorTextureSize", glm::vec2(
                                                                      static_cast<float>(m_sceneColorCopy ? m_sceneColorCopy->GetWidth() : 1),
                                                                      static_cast<float>(m_sceneColorCopy ? m_sceneColorCopy->GetHeight() : 1)));
        m_transparentShader->SetUniform("uSceneColorMaxMipLevel", m_sceneColorCopy ? ResolveMaxMipLevel(m_sceneColorCopy->GetWidth(), m_sceneColorCopy->GetHeight()) : 0.0f);
        BindTransparentEnvironment(m_transparentShader, ctx);
        BindTransparentLights(m_transparentShader, ctx);

        std::vector<TransparentInstanceData> batchInstances;
        batchInstances.reserve(64);
        const RenderCommand *batchHead = nullptr;

        const auto flushBatch = [&]()
        {
            if (!batchHead || batchInstances.empty())
            {
                batchHead = nullptr;
                batchInstances.clear();
                return;
            }

            batchHead->material->Bind(m_transparentShader);
            UploadJointMatrices(m_transparentShader, batchHead->jointMatrices);
            UploadTransparentInstances(m_instanceBuffer, m_instanceCapacity, batchInstances);
            BindTransparentInstanceAttributes(*batchHead->mesh, m_instanceBuffer);
            batchHead->mesh->DrawSubmeshInstancedBound(batchHead->submeshIndex, batchInstances.size(), batchHead->lodIndex);
            batchHead = nullptr;
            batchInstances.clear();
        };

        for (const auto *command : transparentCommands)
        {
            if (command->jointMatrices)
            {
                flushBatch();
                command->material->Bind(m_transparentShader);
                UploadJointMatrices(m_transparentShader, command->jointMatrices);
                std::vector<TransparentInstanceData> instance;
                AppendTransparentInstances(*command, instance);
                UploadTransparentInstances(m_instanceBuffer, m_instanceCapacity, instance);
                BindTransparentInstanceAttributes(*command->mesh, m_instanceBuffer);
                command->mesh->DrawSubmeshInstancedBound(command->submeshIndex, instance.size(), command->lodIndex);
                continue;
            }

            if (batchHead && !CanBatchTransparentCommands(*batchHead, *command))
            {
                flushBatch();
            }

            if (!batchHead)
            {
                batchHead = command;
            }

            AppendTransparentInstances(*command, batchInstances);
        }

        flushBatch();

        m_transparentShader->SetUniform("uUseSkinning", 0);
        m_transparentShader->Unbind();
        Graphics::Disable(GL_BLEND);
        glDepthMask(GL_TRUE);
        Graphics::Enable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glDepthFunc(GL_GREATER);
        Graphics::UnbindRenderTarget();
    }
}
