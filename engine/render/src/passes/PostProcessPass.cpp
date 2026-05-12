#include "PlutoGE/render/passes/PostProcessPass.h"

#include "PlutoGE/render/postprocess/IPostProcessEffect.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/scene/components/CameraComponent.h"

#include <glad/glad.h>

namespace PlutoGE::render
{
    namespace
    {
        bool IsLightingManagedEffect(const IPostProcessEffect *effect)
        {
            return effect && effect->GetTypeName() == "SSGI";
        }

        void BlitColorBuffer(RenderTarget *source, RenderTarget *destination)
        {
            if (!source)
            {
                return;
            }

            glBindFramebuffer(GL_READ_FRAMEBUFFER, source->GetFramebufferID());
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination ? destination->GetFramebufferID() : 0);
            glBlitFramebuffer(
                0, 0, source->GetWidth(), source->GetHeight(),
                0, 0, source->GetWidth(), source->GetHeight(),
                GL_COLOR_BUFFER_BIT,
                GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
    }

    void PostProcessPass::Initialize()
    {
    }

    void PostProcessPass::Execute(const RenderContext &ctx)
    {
        if (!ctx.temporaryRenderTarget)
        {
            return;
        }

        if (!ctx.postProcessEffects)
        {
            BlitColorBuffer(ctx.temporaryRenderTarget, ctx.renderTarget);
            return;
        }

        const auto &effects = *ctx.postProcessEffects;
        if (effects.empty())
        {
            BlitColorBuffer(ctx.temporaryRenderTarget, ctx.renderTarget);
            return;
        }

        size_t enabledEffectCount = 0;
        for (const auto &effect : effects)
        {
            if (effect && effect->IsEnabled() && !IsLightingManagedEffect(effect))
            {
                ++enabledEffectCount;
            }
        }

        if (enabledEffectCount == 0)
        {
            BlitColorBuffer(ctx.temporaryRenderTarget, ctx.renderTarget);
            return;
        }

        RenderTarget *source = ctx.temporaryRenderTarget;
        RenderTarget *scratchA = ctx.temporaryRenderTarget;
        RenderTarget *scratchB = ctx.postProcessIntermediateRenderTarget;
        RenderTarget *nextIntermediate = scratchB;
        size_t appliedEffectCount = 0;

        for (size_t index = 0; index < effects.size(); ++index)
        {
            auto *effect = effects[index];
            if (!effect || !effect->IsEnabled() || IsLightingManagedEffect(effect))
            {
                continue;
            }

            ++appliedEffectCount;
            const bool isLastEffect = appliedEffectCount == enabledEffectCount;
            RenderTarget *destination = isLastEffect ? ctx.renderTarget : nextIntermediate;
            if (!destination)
            {
                continue;
            }

            effect->Apply(PostProcessContext{
                .renderContext = ctx,
                .sourceRenderTarget = source,
                .destinationRenderTarget = destination,
            });

            if (!isLastEffect)
            {
                source = destination;
                nextIntermediate = (destination == scratchA) ? scratchB : scratchA;
            }
        }
    }

}