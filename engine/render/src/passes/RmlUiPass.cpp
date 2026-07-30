#include "PlutoGE/render/passes/RmlUiPass.h"

#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/RmlUiRuntime.h"

#include <PlutoGE_RmlUi_Target.h>

namespace PlutoGE::render
{
    void RmlUiPass::Initialize()
    {
    }

    void RmlUiPass::Execute(const RenderContext &ctx)
    {
        if (!ctx.scene || !ctx.renderer)
            return;

        int width = 0;
        int height = 0;
        if (ctx.renderTarget)
        {
            width = ctx.renderTarget->GetWidth();
            height = ctx.renderTarget->GetHeight();
            Graphics::BindRenderTarget(ctx.renderTarget);
            PlutoGE_SetRmlUiFramebuffer(ctx.renderTarget->GetFramebufferID());
        }
        else if (ctx.temporaryRenderTarget)
        {
            width = ctx.temporaryRenderTarget->GetWidth();
            height = ctx.temporaryRenderTarget->GetHeight();
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            PlutoGE_SetRmlUiFramebuffer(0);
        }

        if (width > 0 && height > 0)
            RmlUiRuntime::Get().Render(*ctx.scene, width, height, ctx.frameSequence);
    }
}
