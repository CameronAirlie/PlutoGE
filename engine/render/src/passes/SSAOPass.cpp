#include "PlutoGE/render/passes/SSAOPass.h"

#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/Renderer.h"

namespace PlutoGE::render
{
    void SSAOPass::Initialize()
    {
        m_ssaoEffect.Initialize();
    }

    void SSAOPass::Execute(const RenderContext &ctx)
    {
        if (!ctx.ambientOcclusionRenderTarget)
        {
            return;
        }

        Graphics::BindRenderTarget(ctx.ambientOcclusionRenderTarget);
        glViewport(0, 0, ctx.ambientOcclusionRenderTarget->GetWidth(), ctx.ambientOcclusionRenderTarget->GetHeight());
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        if (!ctx.gBuffer || !ctx.hasCameraData)
        {
            return;
        }

        m_ssaoEffect.RenderAmbientOcclusion(ctx, ctx.ambientOcclusionRenderTarget);
    }
}