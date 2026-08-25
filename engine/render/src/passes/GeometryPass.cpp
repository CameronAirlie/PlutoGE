#include "PlutoGE/render/passes/GeometryPass.h"
#include "PlutoGE/render/Graphics.h"
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
#include <cstdint>
#include <iostream>
#include <unordered_set>
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

        bool CanBatchGeometryCommands(const RenderCommand &a, std::size_t aLodIndex,
                                      const RenderCommand &b, std::size_t bLodIndex)
        {
            if (a.jointMatrices || b.jointMatrices)
            {
                return false;
            }

            return a.material == b.material &&
                   a.mesh == b.mesh &&
                   a.terrainGeomorph == b.terrainGeomorph &&
                   a.mesh->GetSubmeshLodRange(a.submeshIndex, aLodIndex).indexOffset == b.mesh->GetSubmeshLodRange(b.submeshIndex, bLodIndex).indexOffset &&
                   a.mesh->GetSubmeshLodRange(a.submeshIndex, aLodIndex).indexCount == b.mesh->GetSubmeshLodRange(b.submeshIndex, bLodIndex).indexCount;
        }

        bool IsBlendMaterial(const Material *material)
        {
            return material && material->GetConfig().alphaMode == AlphaMode::Blend;
        }

        glm::vec4 BuildInstanceFlags(const RenderCommand &command, std::size_t lodIndex,
                                     float lodFade, bool incomingLod)
        {
            const float lodMaxIndex = command.mesh ? static_cast<float>(command.mesh->GetSubmeshLodCount(command.submeshIndex) - 1) : 0.0f;
            // Pack the fade into the fractional part of the existing LOD flag;
            // all 16 guaranteed GL 3.3 vertex attributes are already occupied.
            // [0,.25] is the outgoing LOD and [.5,.75] the incoming LOD.
            const float packedLod = static_cast<float>(lodIndex) +
                                    (incomingLod ? 0.5f + lodFade * 0.25f : lodFade * 0.25f);
            return glm::vec4(
                command.terrainGeomorph ? -1.0f :
                    (command.isStatic && command.material && command.material->GetConfig().lightmapTexture ? 1.0f : 0.0f),
                command.usePrimaryUvForLightmap ? 1.0f : 0.0f,
                packedLod,
                lodMaxIndex);
        }

        void AppendGeometryInstances(const RenderCommand &command, std::size_t lodIndex,
                                     float lodFade, bool incomingLod,
                                     std::vector<GeometryInstanceData> &instances)
        {
            if (!command.instanceModels || command.instanceModels->empty())
            {
                instances.push_back(GeometryInstanceData{
                    .model = command.model,
                    .previousModel = command.previousModel,
                    .flags = BuildInstanceFlags(command, lodIndex, lodFade, incomingLod),
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
                    .flags = BuildInstanceFlags(command, lodIndex, lodFade, incomingLod),
                });
            }
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
            std::size_t lodIndex = 0;
            std::size_t firstInstance = 0;
            std::size_t instanceCount = 0;
            bool staticResident = false;
        };

        struct PreparedGeometryGroup
        {
            std::size_t firstDraw = 0;
            std::size_t drawCount = 0;
            std::size_t firstIndirectCommand = 0;
            bool usesIndirect = false;
        };

        void UploadGeometryInstances(unsigned int &instanceBuffer,
                                     std::size_t &instanceCapacity,
                                     const std::vector<GeometryInstanceData> &instances,
                                     GLenum usage = GL_STREAM_DRAW)
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
                glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(instanceCapacity * sizeof(GeometryInstanceData)), nullptr, usage);
            }

            glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(instances.size() * sizeof(GeometryInstanceData)), instances.data());
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
    }

    class GeometryPassScratch
    {
    public:
        std::vector<GeometryInstanceData> instances;
        std::vector<GeometryInstanceData> staticInstances;
        std::unordered_set<const std::vector<glm::mat4> *> submittedStaticSnapshots;
        std::vector<PreparedGeometryDraw> draws;
        std::vector<DrawElementsIndirectCommand> indirectCommands;
        std::vector<PreparedGeometryGroup> groups;
    };

    GeometryPass::GeometryPass()
        : m_scratch(std::make_unique<GeometryPassScratch>())
    {
    }

    GeometryPass::~GeometryPass()
    {
        glDeleteBuffers(static_cast<GLsizei>(m_instanceBuffers.size()), m_instanceBuffers.data());
        glDeleteBuffers(static_cast<GLsizei>(m_indirectBuffers.size()), m_indirectBuffers.data());
        if (m_staticInstanceBuffer)
            glDeleteBuffers(1, &m_staticInstanceBuffer);
    }

    void GeometryPass::Initialize()
    {
        m_geometryPassShader = Shader::CreateGeometryPassShader();
        glGenBuffers(static_cast<GLsizei>(m_instanceBuffers.size()), m_instanceBuffers.data());
        glGenBuffers(static_cast<GLsizei>(m_indirectBuffers.size()), m_indirectBuffers.data());
        glGenBuffers(1, &m_staticInstanceBuffer);
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
        // Opaque geometry must establish its own blend state. Late-frame UI
        // and transparent passes intentionally enable blending and may be the
        // final pass executed in the previous frame.
        Graphics::Disable(GL_BLEND);
        Graphics::Enable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_GEQUAL);
        Graphics::Enable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        Graphics::SetViewport(0, 0, ctx.gBuffer->GetWidth(), ctx.gBuffer->GetHeight());
        // The array index maps directly to the fragment output location. Keep a
        // GL_NONE placeholder for location 5 when LOD debug output is disabled
        // so location 6 still reaches the emission attachment.
        const GLenum defaultAttachments[8] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4, GL_NONE, GL_COLOR_ATTACHMENT6, GL_COLOR_ATTACHMENT7};
        const GLenum debugAttachments[8] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4, GL_COLOR_ATTACHMENT5, GL_COLOR_ATTACHMENT6, GL_COLOR_ATTACHMENT7};
        const bool writeLodDebug = ctx.postProcessDebugView == PostProcessDebugView::Lod;
        glDrawBuffers(8, writeLodDebug ? debugAttachments : defaultAttachments);
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

        auto &instances = m_scratch->instances;
        instances.clear();
        instances.reserve(ctx.renderCommands->size());
        auto &staticInstances = m_scratch->staticInstances;
        auto &draws = m_scratch->draws;
        draws.clear();
        draws.reserve(ctx.renderCommands->size());

        // A static command is resident only when its transform snapshot came
        // directly from submission. Partially visible commands use renderer-owned
        // scratch vectors and deliberately remain on the streamed path.
        auto &submittedStaticSnapshots = m_scratch->submittedStaticSnapshots;
        submittedStaticSnapshots.clear();
        if (ctx.renderer)
        {
            for (const auto &submittedCommand : ctx.renderer->GetSceneRenderCommands())
            {
                if (submittedCommand.isStatic && submittedCommand.instanceModels)
                    submittedStaticSnapshots.insert(submittedCommand.instanceModels.get());
            }
        }
        const auto isStaticResident = [&submittedStaticSnapshots](const RenderCommand &command)
        {
            return command.isStatic && !command.jointMatrices &&
                   command.instanceModels &&
                   submittedStaticSnapshots.contains(command.instanceModels.get());
        };
        std::uint64_t staticSignature = 1469598103934665603ull;
        std::size_t expectedStaticInstanceCount = 0;
        const auto combineStaticSignature = [&staticSignature](std::uint64_t value)
        {
            staticSignature ^= value + 0x9e3779b97f4a7c15ull +
                               (staticSignature << 6u) + (staticSignature >> 2u);
        };
        for (const auto &command : *ctx.renderCommands)
        {
            if (!command.material || !command.mesh || IsBlendMaterial(command.material) ||
                !isStaticResident(command))
                continue;
            combineStaticSignature(reinterpret_cast<std::uintptr_t>(command.instanceModels.get()));
            combineStaticSignature(reinterpret_cast<std::uintptr_t>(command.previousInstanceModels.get()));
            combineStaticSignature(command.instanceModels->size());
            combineStaticSignature(command.submeshIndex);
            combineStaticSignature(command.lodIndex);
            combineStaticSignature(command.minLodIndex);
            combineStaticSignature(command.usePrimaryUvForLightmap ? 1u : 0u);
            combineStaticSignature(command.terrainGeomorph ? 1u : 0u);
            expectedStaticInstanceCount += command.instanceModels->size();
            if (command.GetLodTransitionIndex() != command.lodIndex &&
                command.GetLodTransitionFade() > 0.0f && command.GetLodTransitionFade() < 1.0f)
            {
                if (!command.terrainGeomorph)
                    expectedStaticInstanceCount += command.instanceModels->size();
            }
        }
        combineStaticSignature(expectedStaticInstanceCount);
        const bool rebuildStaticInstances = staticSignature != m_staticInstanceSignature ||
                                            expectedStaticInstanceCount != m_staticInstanceCount;
        if (rebuildStaticInstances)
        {
            staticInstances.clear();
            staticInstances.reserve(expectedStaticInstanceCount);
        }
        std::size_t staticInstanceCursor = 0;

        // Build one contiguous instance stream for the entire pass. Uploading and
        // orphaning the buffer once per draw is especially expensive in scenes
        // with many one-instance batches.
        for (const auto &command : *ctx.renderCommands)
        {
            if (!command.material || !command.mesh || IsBlendMaterial(command.material))
            {
                continue;
            }

            const auto appendDraw = [&](std::size_t lodIndex, float lodFade, bool incomingLod)
            {
                const bool resident = isStaticResident(command);
                const bool appendToPrevious = !command.jointMatrices &&
                                              !draws.empty() &&
                                              !draws.back().command->jointMatrices &&
                                              draws.back().staticResident == resident &&
                                              CanBatchGeometryCommands(*draws.back().command, draws.back().lodIndex,
                                                                       command, lodIndex);
                if (!appendToPrevious)
                {
                    draws.push_back(PreparedGeometryDraw{
                        .command = &command,
                        .lodIndex = lodIndex,
                        .firstInstance = resident ? staticInstanceCursor : instances.size(),
                        .staticResident = resident,
                    });
                }

                if (resident)
                {
                    const std::size_t instanceCount = command.instanceModels->size();
                    if (rebuildStaticInstances)
                        AppendGeometryInstances(command, lodIndex, lodFade, incomingLod, staticInstances);
                    staticInstanceCursor += instanceCount;
                    draws.back().instanceCount += instanceCount;
                }
                else
                {
                    const std::size_t previousInstanceCount = instances.size();
                    AppendGeometryInstances(command, lodIndex, lodFade, incomingLod, instances);
                    draws.back().instanceCount += instances.size() - previousInstanceCount;
                }
            };

            const std::size_t transitionLodIndex = command.GetLodTransitionIndex();
            const float transitionFade = command.GetLodTransitionFade();
            const bool transitioning = transitionLodIndex != command.lodIndex &&
                                       transitionFade > 0.0f &&
                                       transitionFade < 1.0f;
            appendDraw(command.lodIndex, transitioning ? transitionFade : 0.0f, false);
            if (transitioning && !command.terrainGeomorph)
            {
                appendDraw(transitionLodIndex, transitionFade, true);
            }
        }

        if (rebuildStaticInstances)
        {
            UploadGeometryInstances(m_staticInstanceBuffer, m_staticInstanceCapacity,
                                    staticInstances, GL_STATIC_DRAW);
            m_staticInstanceSignature = staticSignature;
            m_staticInstanceCount = expectedStaticInstanceCount;
        }

        const std::size_t streamBufferIndex = m_streamBufferIndex;
        m_streamBufferIndex = (m_streamBufferIndex + 1) % kStreamBufferCount;
        auto &instanceBuffer = m_instanceBuffers[streamBufferIndex];
        auto &instanceCapacity = m_instanceCapacities[streamBufferIndex];
        auto &indirectBuffer = m_indirectBuffers[streamBufferIndex];
        auto &indirectCapacity = m_indirectCapacities[streamBufferIndex];
        UploadGeometryInstances(instanceBuffer, instanceCapacity, instances);

        auto &indirectCommands = m_scratch->indirectCommands;
        indirectCommands.clear();
        indirectCommands.reserve(draws.size());
        auto &groups = m_scratch->groups;
        groups.clear();
        groups.reserve(draws.size());
        for (std::size_t drawIndex = 0; drawIndex < draws.size();)
        {
            const auto &head = draws[drawIndex];
            const bool canUseIndirect = !head.command->jointMatrices;
            const IndirectDrawGroupingKey headKey{
                .shader = head.command->material ? head.command->material->GetShader() : head.command->shader,
                .material = head.command->material,
                .mesh = head.command->mesh,
                .skinned = head.command->jointMatrices != nullptr,
            };
            std::size_t drawEnd = drawIndex + 1;
            if (canUseIndirect)
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
                    if (draws[drawEnd].staticResident != head.staticResident ||
                        !CanGroupGeometryIndirectDraws(headKey, candidateKey))
                    {
                        break;
                    }
                    ++drawEnd;
                }
            }

            const bool usesIndirect = canUseIndirect && drawEnd - drawIndex > 1;
            const std::size_t firstIndirectCommand = indirectCommands.size();
            if (usesIndirect)
            {
                for (std::size_t groupedDrawIndex = drawIndex; groupedDrawIndex < drawEnd; ++groupedDrawIndex)
                {
                    const auto &groupedDraw = draws[groupedDrawIndex];
                    const auto range = groupedDraw.command->mesh->GetSubmeshLodRange(
                        groupedDraw.command->submeshIndex,
                        groupedDraw.lodIndex);
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

        UploadIndirectDrawCommands(indirectBuffer, indirectCapacity, indirectCommands);

        Material *boundMaterial = nullptr;
        Mesh *boundMesh = nullptr;
        unsigned int boundInstanceBuffer = 0;
        bool skinningEnabled = false;
        const std::vector<glm::mat4> *boundJointMatrices = nullptr;
        bool bakedLightingWriteEnabled = true;
        bool emissionWriteEnabled = true;
        int apiDrawCalls = 0;
        for (const auto &group : groups)
        {
            const auto &draw = draws[group.firstDraw];
            const auto &command = *draw.command;
            const unsigned int groupInstanceBuffer = draw.staticResident
                                                         ? m_staticInstanceBuffer
                                                         : instanceBuffer;
            if (command.material != boundMaterial)
            {
                Shader *previousShader = activeShader;
                Shader *shader = bindGeometryShader(command.material->GetShader());
                command.material->Bind(shader);
                boundMaterial = command.material;

                // The HDR auxiliary targets were cleared above. Avoid writing
                // their zero defaults for ordinary materials; at high
                // resolutions this removes substantial G-buffer traffic per
                // covered sample. Custom shaders remain fully enabled
                // because their graph outputs cannot be inferred here.
                const auto &materialConfig = command.material->GetConfig();
                if (materialConfig.twoSided)
                {
                    Graphics::Disable(GL_CULL_FACE);
                }
                else
                {
                    Graphics::Enable(GL_CULL_FACE);
                    glCullFace(GL_BACK);
                }
                const bool usesCustomShader = command.material->GetShader() != nullptr;
                const bool shouldWriteBakedLighting = usesCustomShader || materialConfig.lightmapTexture != nullptr;
                const bool shouldWriteEmission = usesCustomShader ||
                                                 glm::any(glm::greaterThan(materialConfig.emission, glm::vec3(0.0f)));
                if (bakedLightingWriteEnabled != shouldWriteBakedLighting)
                {
                    glColorMaski(4, shouldWriteBakedLighting, shouldWriteBakedLighting,
                                shouldWriteBakedLighting, shouldWriteBakedLighting);
                    bakedLightingWriteEnabled = shouldWriteBakedLighting;
                }
                if (emissionWriteEnabled != shouldWriteEmission)
                {
                    glColorMaski(6, shouldWriteEmission, shouldWriteEmission,
                                shouldWriteEmission, shouldWriteEmission);
                    emissionWriteEnabled = shouldWriteEmission;
                }
                if (activeShader != previousShader)
                {
                    skinningEnabled = false;
                    boundJointMatrices = nullptr;
                }
            }

            if (group.usesIndirect)
            {
                if (skinningEnabled)
                {
                    activeShader->SetUniform("uUseSkinning", 0);
                    skinningEnabled = false;
                    boundJointMatrices = nullptr;
                }
                if (command.mesh != boundMesh || groupInstanceBuffer != boundInstanceBuffer)
                {
                    BindGeometryInstanceAttributes(*command.mesh, groupInstanceBuffer, 0);
                    boundMesh = command.mesh;
                    boundInstanceBuffer = groupInstanceBuffer;
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
                    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirectBuffer);
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
                            directDraw.lodIndex);
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
                if (command.jointMatrices)
                {
                    if (command.jointMatrices != boundJointMatrices)
                    {
                        UploadJointMatrices(activeShader, command.jointMatrices);
                        boundJointMatrices = command.jointMatrices;
                    }
                    skinningEnabled = true;
                }
                else if (skinningEnabled)
                {
                    activeShader->SetUniform("uUseSkinning", 0);
                    skinningEnabled = false;
                    boundJointMatrices = nullptr;
                }
                BindGeometryInstanceAttributes(*command.mesh, groupInstanceBuffer, 0);
                command.mesh->DrawSubmeshInstancedBaseInstanceBound(command.submeshIndex,
                                                                    draw.instanceCount,
                                                                    draw.firstInstance,
                                                                    draw.lodIndex);
                boundMesh = command.mesh;
                boundInstanceBuffer = groupInstanceBuffer;
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
                    const auto indexCount = groupedCommand.mesh->GetSubmeshLodIndexCount(groupedCommand.submeshIndex, groupedDraw.lodIndex);
                    ctx.renderer->RecordGeometryBatch(
                        static_cast<int>(groupedDraw.instanceCount),
                        static_cast<int>((indexCount / 3) * groupedDraw.instanceCount),
                        groupedDraw.lodIndex);
                }
            }
        }

        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
        // Color masks are context state and must not leak into later passes or
        // the next frame's G-buffer clear.
        if (!bakedLightingWriteEnabled)
        {
            glColorMaski(4, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        }
        if (!emissionWriteEnabled)
        {
            glColorMaski(6, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        }
        Graphics::Enable(GL_CULL_FACE);
        glCullFace(GL_BACK);
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
