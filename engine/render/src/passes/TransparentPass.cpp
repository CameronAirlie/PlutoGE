#include "PlutoGE/render/passes/TransparentPass.h"

#include "PlutoGE/render/GBuffer.h"
#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/Shader.h"

#include <algorithm>
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
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ctx.temporaryRenderTarget->GetFramebufferID());
        glBlitFramebuffer(
            0, 0, ctx.gBuffer->GetWidth(), ctx.gBuffer->GetHeight(),
            0, 0, ctx.temporaryRenderTarget->GetWidth(), ctx.temporaryRenderTarget->GetHeight(),
            GL_DEPTH_BUFFER_BIT,
            GL_NEAREST);

        Graphics::BindRenderTarget(ctx.temporaryRenderTarget);
        glViewport(0, 0, ctx.temporaryRenderTarget->GetWidth(), ctx.temporaryRenderTarget->GetHeight());
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        m_transparentShader->Bind();
        m_transparentShader->SetUniform("uView", ctx.cameraData.view);
        m_transparentShader->SetUniform("uProjection", ctx.cameraData.projection);

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
