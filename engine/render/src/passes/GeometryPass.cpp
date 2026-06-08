#include "PlutoGE/render/passes/GeometryPass.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/GBuffer.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Renderer.h"

#include <array>
#include <cstddef>
#include <chrono>
#include <vector>

namespace PlutoGE::render
{
    namespace
    {
        constexpr bool kEnableVisibilityCulling = true;

        struct FrustumPlane
        {
            glm::vec3 normal{0.0f};
            float distance = 0.0f;
        };

        struct GeometryInstanceData
        {
            glm::mat4 model{1.0f};
            glm::mat4 previousModel{1.0f};
            glm::vec4 flags{0.0f};
        };

        std::array<FrustumPlane, 6> ExtractFrustumPlanes(const glm::mat4 &viewProjection)
        {
            std::array<FrustumPlane, 6> planes = {
                FrustumPlane{glm::vec3(viewProjection[0][3] + viewProjection[0][0], viewProjection[1][3] + viewProjection[1][0], viewProjection[2][3] + viewProjection[2][0]), viewProjection[3][3] + viewProjection[3][0]},
                FrustumPlane{glm::vec3(viewProjection[0][3] - viewProjection[0][0], viewProjection[1][3] - viewProjection[1][0], viewProjection[2][3] - viewProjection[2][0]), viewProjection[3][3] - viewProjection[3][0]},
                FrustumPlane{glm::vec3(viewProjection[0][3] + viewProjection[0][1], viewProjection[1][3] + viewProjection[1][1], viewProjection[2][3] + viewProjection[2][1]), viewProjection[3][3] + viewProjection[3][1]},
                FrustumPlane{glm::vec3(viewProjection[0][3] - viewProjection[0][1], viewProjection[1][3] - viewProjection[1][1], viewProjection[2][3] - viewProjection[2][1]), viewProjection[3][3] - viewProjection[3][1]},
                FrustumPlane{glm::vec3(viewProjection[0][3] + viewProjection[0][2], viewProjection[1][3] + viewProjection[1][2], viewProjection[2][3] + viewProjection[2][2]), viewProjection[3][3] + viewProjection[3][2]},
                FrustumPlane{glm::vec3(viewProjection[0][3] - viewProjection[0][2], viewProjection[1][3] - viewProjection[1][2], viewProjection[2][3] - viewProjection[2][2]), viewProjection[3][3] - viewProjection[3][2]},
            };

            for (auto &plane : planes)
            {
                const float length = glm::length(plane.normal);
                if (length > 1e-6f)
                {
                    plane.normal /= length;
                    plane.distance /= length;
                }
            }

            return planes;
        }

        bool IsSubmeshVisible(const RenderCommand &command, const std::array<FrustumPlane, 6> &planes)
        {
            if (!command.mesh)
            {
                return false;
            }

            for (const auto &plane : planes)
            {
                if (glm::dot(plane.normal, command.worldBounds.center) + plane.distance < -command.worldBounds.radius)
                {
                    return false;
                }
            }

            return true;
        }

        bool CanBatchGeometryCommands(const RenderCommand &a, const RenderCommand &b)
        {
            return a.material == b.material &&
                   a.mesh == b.mesh &&
                   a.submeshIndex == b.submeshIndex;
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

        void BindGeometryInstanceAttributes(const Mesh &mesh, unsigned int instanceBuffer)
        {
            glBindVertexArray(mesh.GetVAO());
            glBindBuffer(GL_ARRAY_BUFFER, instanceBuffer);
            ConfigureMatrixAttributes(5, offsetof(GeometryInstanceData, model), sizeof(GeometryInstanceData));
            ConfigureMatrixAttributes(9, offsetof(GeometryInstanceData, previousModel), sizeof(GeometryInstanceData));
            glEnableVertexAttribArray(13);
            glVertexAttribPointer(13, 4, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(sizeof(GeometryInstanceData)), reinterpret_cast<const void *>(offsetof(GeometryInstanceData, flags)));
            glVertexAttribDivisor(13, 1);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }

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
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        m_geometryPassShader->Bind();
        m_geometryPassShader->SetUniform("uView", ctx.cameraData.view);
        m_geometryPassShader->SetUniform("uProjection", ctx.cameraData.projection);
        const glm::mat4 currentViewProjection = ctx.cameraData.projection * ctx.cameraData.view;
        const glm::mat4 previousViewProjection = ctx.hasPreviousCameraData
                                                     ? ctx.previousCameraData.projection * ctx.previousCameraData.view
                                                     : currentViewProjection;
        m_geometryPassShader->SetUniform("uCurrentViewProjection", currentViewProjection);
        m_geometryPassShader->SetUniform("uPreviousViewProjection", previousViewProjection);
        const auto frustumPlanes = ExtractFrustumPlanes(ctx.cameraData.projection * ctx.cameraData.view);
        Material *boundMaterial = nullptr;
        Mesh *boundMesh = nullptr;
        std::vector<GeometryInstanceData> batchInstances;
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

            if (batchHead->material != boundMaterial)
            {
                batchHead->material->Bind(m_geometryPassShader);
                boundMaterial = batchHead->material;
            }

            UploadGeometryInstances(m_instanceBuffer, m_instanceCapacity, batchInstances);
            if (batchHead->mesh != boundMesh)
            {
                BindGeometryInstanceAttributes(*batchHead->mesh, m_instanceBuffer);
                boundMesh = batchHead->mesh;
            }

            batchHead->mesh->DrawSubmeshInstancedBound(batchHead->submeshIndex, batchInstances.size());
            batchHead = nullptr;
            batchInstances.clear();
        };

        for (const auto &command : *ctx.renderCommands)
        {
            if (!command.material || !command.mesh)
            {
                continue;
            }

            if (batchHead && !CanBatchGeometryCommands(*batchHead, command))
            {
                flushBatch();
            }

            if (kEnableVisibilityCulling && !IsSubmeshVisible(command, frustumPlanes))
            {
                continue;
            }

            if (!batchHead)
            {
                batchHead = &command;
            }

            batchInstances.push_back(GeometryInstanceData{
                .model = command.model,
                .previousModel = command.previousModel,
                .flags = glm::vec4(command.isStatic ? 1.0f : 0.0f, command.usePrimaryUvForLightmap ? 1.0f : 0.0f, 0.0f, 0.0f),
            });
        }

        flushBatch();

        m_geometryPassShader->Unbind();
        ctx.gBuffer->Unbind();
    }
}
