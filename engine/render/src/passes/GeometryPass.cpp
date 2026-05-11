#include "PlutoGE/render/passes/GeometryPass.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/GBuffer.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Renderer.h"

#include <array>
#include <chrono>

namespace PlutoGE::render
{
    namespace
    {
        constexpr bool kEnableVisibilityCulling = false;

        struct FrustumPlane
        {
            glm::vec3 normal{0.0f};
            float distance = 0.0f;
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

            const auto &bounds = command.submeshIndex < command.mesh->GetSubmeshCount()
                                     ? command.mesh->GetSubmesh(command.submeshIndex).bounds
                                     : command.mesh->GetBounds();
            const glm::vec3 localCenter = bounds.center;
            const glm::vec3 worldCenter = glm::vec3(command.model * glm::vec4(localCenter, 1.0f));
            const float scaleX = glm::length(glm::vec3(command.model[0]));
            const float scaleY = glm::length(glm::vec3(command.model[1]));
            const float scaleZ = glm::length(glm::vec3(command.model[2]));
            const float worldRadius = bounds.radius * std::max(scaleX, std::max(scaleY, scaleZ));

            for (const auto &plane : planes)
            {
                if (glm::dot(plane.normal, worldCenter) + plane.distance < -worldRadius)
                {
                    return false;
                }
            }

            return true;
        }
    }

    void GeometryPass::Initialize()
    {
        m_geometryPassShader = Shader::CreateGeometryPassShader();
    }

    void GeometryPass::Execute(const RenderContext &ctx)
    {
        if (!ctx.gBuffer->IsInitialized() ||
            ctx.gBuffer->GetWidth() != ctx.renderTarget->GetWidth() ||
            ctx.gBuffer->GetHeight() != ctx.renderTarget->GetHeight())
        {
            const auto resizeStart = std::chrono::high_resolution_clock::now();
            ctx.gBuffer->Cleanup();
            ctx.gBuffer->Initialize(ctx.renderTarget->GetWidth(), ctx.renderTarget->GetHeight());
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
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        m_geometryPassShader->Bind();
        m_geometryPassShader->SetUniform("uView", ctx.cameraData.view);
        m_geometryPassShader->SetUniform("uProjection", ctx.cameraData.projection);
        const auto frustumPlanes = ExtractFrustumPlanes(ctx.cameraData.projection * ctx.cameraData.view);
        Material *boundMaterial = nullptr;

        for (const auto &command : *ctx.renderCommands)
        {
            if (kEnableVisibilityCulling && !IsSubmeshVisible(command, frustumPlanes))
            {
                continue;
            }

            m_geometryPassShader->SetUniform("uModel", command.model);

            if (command.material != boundMaterial)
            {
                command.material->Bind(m_geometryPassShader);
                boundMaterial = command.material;
            }

            command.mesh->DrawSubmesh(command.submeshIndex);
        }

        m_geometryPassShader->Unbind();
        ctx.gBuffer->Unbind();
    }
}