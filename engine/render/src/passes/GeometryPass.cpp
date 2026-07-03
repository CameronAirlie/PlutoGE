#include "PlutoGE/render/passes/GeometryPass.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/GBuffer.h"
#include "PlutoGE/render/IndirectDraw.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Renderer.h"

#include <array>
#include <cstddef>
#include <chrono>
#include <iostream>
#include <vector>

namespace PlutoGE::render
{
    namespace
    {
        struct GeometryInstanceData
        {
            glm::mat4 model{1.0f};
            glm::mat4 previousModel{1.0f};
            glm::vec4 flags{0.0f};
        };

        bool CanBatchGeometryCommands(const RenderCommand &a, const RenderCommand &b)
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

        bool IsBlendMaterial(const Material *material)
        {
            return material && material->GetConfig().alphaMode == AlphaMode::Blend;
        }

        glm::vec4 BuildInstanceFlags(const RenderCommand &command)
        {
            const float lodMaxIndex = command.mesh ? static_cast<float>(command.mesh->GetSubmeshLodCount(command.submeshIndex) - 1) : 0.0f;
            return glm::vec4(
                command.isStatic ? 1.0f : 0.0f,
                command.usePrimaryUvForLightmap ? 1.0f : 0.0f,
                static_cast<float>(command.lodIndex),
                lodMaxIndex);
        }

        void AppendGeometryInstances(const RenderCommand &command, std::vector<GeometryInstanceData> &instances)
        {
            if (!command.instanceModels || command.instanceModels->empty())
            {
                instances.push_back(GeometryInstanceData{
                    .model = command.model,
                    .previousModel = command.previousModel,
                    .flags = BuildInstanceFlags(command),
                });
                return;
            }

            instances.reserve(instances.size() + command.instanceModels->size());
            for (std::size_t index = 0; index < command.instanceModels->size(); ++index)
            {
                const glm::mat4 &model = (*command.instanceModels)[index];
                const glm::mat4 &previousModel = command.previousInstanceModels && index < command.previousInstanceModels->size()
                                                     ? (*command.previousInstanceModels)[index]
                                                     : model;
                instances.push_back(GeometryInstanceData{
                    .model = model,
                    .previousModel = previousModel,
                    .flags = BuildInstanceFlags(command),
                });
            }
        }

        void UploadJointMatrices(Shader *shader, const std::vector<glm::mat4> *jointMatrices)
        {
            constexpr size_t kMaxShaderJoints = 128;
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

        void BindGeometryInstanceAttributes(const Mesh &mesh, unsigned int instanceBuffer, std::size_t firstInstance)
        {
            const std::size_t baseOffset = firstInstance * sizeof(GeometryInstanceData);
            glBindVertexArray(mesh.GetVAO());
            glBindBuffer(GL_ARRAY_BUFFER, instanceBuffer);
            ConfigureMatrixAttributes(5, baseOffset + offsetof(GeometryInstanceData, model), sizeof(GeometryInstanceData));
            ConfigureMatrixAttributes(9, baseOffset + offsetof(GeometryInstanceData, previousModel), sizeof(GeometryInstanceData));
            glEnableVertexAttribArray(13);
            glVertexAttribPointer(13, 4, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(sizeof(GeometryInstanceData)), reinterpret_cast<const void *>(baseOffset + offsetof(GeometryInstanceData, flags)));
            glVertexAttribDivisor(13, 1);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }

        struct PreparedGeometryDraw
        {
            const RenderCommand *command = nullptr;
            std::size_t firstInstance = 0;
            std::size_t instanceCount = 0;
        };

        void UploadGeometryInstances(unsigned int &instanceBuffer,
                                     std::size_t &instanceCapacity,
                                     const std::vector<GeometryInstanceData> &instances)
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

            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(instanceCapacity * sizeof(GeometryInstanceData)), nullptr, GL_STREAM_DRAW);
            glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(instances.size() * sizeof(GeometryInstanceData)), instances.data());
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
    }

    void GeometryPass::Initialize()
    {
        m_geometryPassShader = Shader::CreateGeometryPassShader();
        if (m_instanceBuffer == 0)
        {
            glGenBuffers(1, &m_instanceBuffer);
        }
    }

    void GeometryPass::Execute(const RenderContext &ctx)
    {
        RenderTarget *geometryTarget = ctx.renderTarget ? ctx.renderTarget : ctx.temporaryRenderTarget;
        if (!ctx.gBuffer || !geometryTarget)
        {
            return;
        }

        if (!ctx.gBuffer->IsInitialized() ||
            ctx.gBuffer->GetWidth() != geometryTarget->GetWidth() ||
            ctx.gBuffer->GetHeight() != geometryTarget->GetHeight())
        {
            const auto resizeStart = std::chrono::high_resolution_clock::now();
            ctx.gBuffer->Cleanup();
            ctx.gBuffer->Initialize(geometryTarget->GetWidth(), geometryTarget->GetHeight());
            const auto resizeEnd = std::chrono::high_resolution_clock::now();
            if (ctx.renderer)
            {
                ctx.renderer->RecordGBufferResize(std::chrono::duration<float, std::milli>(resizeEnd - resizeStart).count());
            }
        }

        ctx.gBuffer->Bind();
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glViewport(0, 0, ctx.gBuffer->GetWidth(), ctx.gBuffer->GetHeight());
        // The array index maps directly to the fragment output location. Keep a
        // GL_NONE placeholder for location 5 when LOD debug output is disabled
        // so location 6 still reaches the emission attachment.
        const GLenum defaultAttachments[7] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4, GL_NONE, GL_COLOR_ATTACHMENT6};
        const GLenum debugAttachments[7] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4, GL_COLOR_ATTACHMENT5, GL_COLOR_ATTACHMENT6};
        const bool writeLodDebug = ctx.postProcessDebugView == PostProcessDebugView::Lod;
        glDrawBuffers(7, writeLodDebug ? debugAttachments : defaultAttachments);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (writeLodDebug)
        {
            const GLfloat noLodDebugValue[4] = {-1.0f, 0.0f, 0.0f, 0.0f};
            glClearBufferfv(GL_COLOR, 5, noLodDebugValue);
        }

        const glm::mat4 currentViewProjection = ctx.cameraData.projection * ctx.cameraData.view;
        const glm::mat4 previousViewProjection = ctx.hasPreviousCameraData
                                                     ? ctx.previousCameraData.projection * ctx.previousCameraData.view
                                                     : currentViewProjection;
        Shader *activeShader = nullptr;
        const auto bindGeometryShader = [&](Shader *shader)
        {
            shader = shader ? shader : m_geometryPassShader;
            if (activeShader == shader)
            {
                return shader;
            }

            shader->Bind();
            shader->SetUniform("uView", ctx.cameraData.view);
            shader->SetUniform("uProjection", ctx.cameraData.projection);
            shader->SetUniform("uCurrentViewProjection", currentViewProjection);
            shader->SetUniform("uPreviousViewProjection", previousViewProjection);
            shader->SetUniform("uUseSkinning", 0);
            activeShader = shader;
            return shader;
        };

        bindGeometryShader(m_geometryPassShader);

        std::vector<GeometryInstanceData> instances;
        instances.reserve(ctx.renderCommands->size());
        std::vector<PreparedGeometryDraw> draws;
        draws.reserve(ctx.renderCommands->size());

        // Build one contiguous instance stream for the entire pass. Uploading and
        // orphaning the buffer once per draw is especially expensive in scenes
        // with many one-instance batches.
        for (const auto &command : *ctx.renderCommands)
        {
            if (!command.material || !command.mesh || IsBlendMaterial(command.material))
            {
                continue;
            }

            const bool appendToPrevious = !command.jointMatrices &&
                                          !draws.empty() &&
                                          !draws.back().command->jointMatrices &&
                                          CanBatchGeometryCommands(*draws.back().command, command);
            if (!appendToPrevious)
            {
                draws.push_back(PreparedGeometryDraw{
                    .command = &command,
                    .firstInstance = instances.size(),
                });
            }

            const std::size_t previousInstanceCount = instances.size();
            AppendGeometryInstances(command, instances);
            draws.back().instanceCount += instances.size() - previousInstanceCount;
        }

        UploadGeometryInstances(m_instanceBuffer, m_instanceCapacity, instances);

        struct PreparedGeometryGroup
        {
            std::size_t firstDraw = 0;
            std::size_t drawCount = 0;
            std::size_t firstIndirectCommand = 0;
            bool usesIndirect = false;
        };

        std::vector<DrawElementsIndirectCommand> indirectCommands;
        indirectCommands.reserve(draws.size());
        std::vector<PreparedGeometryGroup> groups;
        groups.reserve(draws.size());
        for (std::size_t drawIndex = 0; drawIndex < draws.size();)
        {
            const auto &head = draws[drawIndex];
            const bool usesIndirect = !head.command->jointMatrices;
            const IndirectDrawGroupingKey headKey{
                .shader = head.command->material ? head.command->material->GetShader() : head.command->shader,
                .material = head.command->material,
                .mesh = head.command->mesh,
                .skinned = head.command->jointMatrices != nullptr,
            };
            std::size_t drawEnd = drawIndex + 1;
            if (usesIndirect)
            {
                while (drawEnd < draws.size())
                {
                    const auto *candidate = draws[drawEnd].command;
                    const IndirectDrawGroupingKey candidateKey{
                        .shader = candidate->material ? candidate->material->GetShader() : candidate->shader,
                        .material = candidate->material,
                        .mesh = candidate->mesh,
                        .skinned = candidate->jointMatrices != nullptr,
                    };
                    if (!CanGroupGeometryIndirectDraws(headKey, candidateKey))
                    {
                        break;
                    }
                    ++drawEnd;
                }
            }

            const std::size_t firstIndirectCommand = indirectCommands.size();
            if (usesIndirect)
            {
                for (std::size_t groupedDrawIndex = drawIndex; groupedDrawIndex < drawEnd; ++groupedDrawIndex)
                {
                    const auto &groupedDraw = draws[groupedDrawIndex];
                    const auto range = groupedDraw.command->mesh->GetSubmeshLodRange(
                        groupedDraw.command->submeshIndex,
                        groupedDraw.command->lodIndex);
                    indirectCommands.push_back(BuildDrawElementsIndirectCommand(
                        range.indexCount,
                        groupedDraw.instanceCount,
                        range.indexOffset,
                        groupedDraw.firstInstance));
                }
            }

            groups.push_back(PreparedGeometryGroup{
                .firstDraw = drawIndex,
                .drawCount = drawEnd - drawIndex,
                .firstIndirectCommand = firstIndirectCommand,
                .usesIndirect = usesIndirect,
            });
            drawIndex = drawEnd;
        }

        UploadIndirectDrawCommands(m_indirectBuffer, m_indirectCapacity, indirectCommands);

        Material *boundMaterial = nullptr;
        Mesh *boundMesh = nullptr;
        bool skinningEnabled = false;
        int apiDrawCalls = 0;
        for (const auto &group : groups)
        {
            const auto &draw = draws[group.firstDraw];
            const auto &command = *draw.command;
            if (command.material != boundMaterial)
            {
                Shader *previousShader = activeShader;
                Shader *shader = bindGeometryShader(command.material->GetShader());
                command.material->Bind(shader);
                boundMaterial = command.material;
                if (activeShader != previousShader)
                {
                    skinningEnabled = false;
                }
            }

            if (group.usesIndirect)
            {
                if (skinningEnabled)
                {
                    activeShader->SetUniform("uUseSkinning", 0);
                    skinningEnabled = false;
                }
                if (command.mesh != boundMesh)
                {
                    BindGeometryInstanceAttributes(*command.mesh, m_instanceBuffer, 0);
                    boundMesh = command.mesh;
                }
                bool submittedIndirectly = false;
                if (m_indirectDrawEnabled)
                {
                    const bool validateIndirectDraw = !m_indirectDrawValidated;
                    if (validateIndirectDraw)
                    {
                        while (glGetError() != GL_NO_ERROR)
                        {
                        }
                    }
                    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_indirectBuffer);
                    glMultiDrawElementsIndirect(
                        GL_TRIANGLES,
                        GL_UNSIGNED_INT,
                        reinterpret_cast<const void *>(group.firstIndirectCommand * sizeof(DrawElementsIndirectCommand)),
                        static_cast<GLsizei>(group.drawCount),
                        0);
                    if (validateIndirectDraw)
                    {
                        const GLenum indirectError = glGetError();
                        submittedIndirectly = indirectError == GL_NO_ERROR;
                        if (!submittedIndirectly)
                        {
                            std::cerr << "Geometry multi-draw disabled after OpenGL error " << indirectError
                                      << "; using direct draws." << std::endl;
                            m_indirectDrawEnabled = false;
                        }
                        else
                        {
                            m_indirectDrawValidated = true;
                        }
                    }
                    else
                    {
                        submittedIndirectly = true;
                    }
                }

                if (!submittedIndirectly)
                {
                    for (std::size_t groupedDrawIndex = group.firstDraw;
                         groupedDrawIndex < group.firstDraw + group.drawCount;
                         ++groupedDrawIndex)
                    {
                        const auto &directDraw = draws[groupedDrawIndex];
                        directDraw.command->mesh->DrawSubmeshInstancedBaseInstanceBound(
                            directDraw.command->submeshIndex,
                            directDraw.instanceCount,
                            directDraw.firstInstance,
                            directDraw.command->lodIndex);
                    }
                    apiDrawCalls += static_cast<int>(group.drawCount);
                }
                else
                {
                    ++apiDrawCalls;
                }
            }
            else
            {
                UploadJointMatrices(activeShader, command.jointMatrices);
                skinningEnabled = true;
                BindGeometryInstanceAttributes(*command.mesh, m_instanceBuffer, 0);
                command.mesh->DrawSubmeshInstancedBaseInstanceBound(command.submeshIndex,
                                                                    draw.instanceCount,
                                                                    draw.firstInstance,
                                                                    command.lodIndex);
                boundMesh = command.mesh;
                ++apiDrawCalls;
            }

            if (ctx.renderer)
            {
                for (std::size_t groupedDrawIndex = group.firstDraw;
                     groupedDrawIndex < group.firstDraw + group.drawCount;
                     ++groupedDrawIndex)
                {
                    const auto &groupedDraw = draws[groupedDrawIndex];
                    const auto &groupedCommand = *groupedDraw.command;
                    const auto indexCount = groupedCommand.mesh->GetSubmeshLodIndexCount(groupedCommand.submeshIndex, groupedCommand.lodIndex);
                    ctx.renderer->RecordGeometryBatch(
                        static_cast<int>(groupedDraw.instanceCount),
                        static_cast<int>((indexCount / 3) * groupedDraw.instanceCount),
                        groupedCommand.lodIndex);
                }
            }
        }

        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
        if (ctx.renderer)
        {
            ctx.renderer->RecordGeometryDriverSubmission(static_cast<int>(groups.size()), apiDrawCalls);
        }

        if (activeShader)
        {
            activeShader->Unbind();
        }
        ctx.gBuffer->Unbind();
    }
}
