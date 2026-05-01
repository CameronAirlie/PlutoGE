#include "PlutoGE/render/passes/GeometryPass.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/GBuffer.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Renderer.h"

namespace PlutoGE::render
{
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
            ctx.gBuffer->Cleanup();
            ctx.gBuffer->Initialize(ctx.renderTarget->GetWidth(), ctx.renderTarget->GetHeight());
        }

        ctx.gBuffer->Bind();
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glViewport(0, 0, ctx.gBuffer->GetWidth(), ctx.gBuffer->GetHeight());
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        m_geometryPassShader->Bind();

        for (const auto &command : *ctx.renderCommands)
        {
            m_geometryPassShader->SetUniform("uView", ctx.cameraData.view);
            m_geometryPassShader->SetUniform("uProjection", ctx.cameraData.projection);
            m_geometryPassShader->SetUniform("uModel", command.model);

            command.material->Bind(m_geometryPassShader);
            command.mesh->Draw();
        }

        m_geometryPassShader->Unbind();
        ctx.gBuffer->Unbind();
    }
}