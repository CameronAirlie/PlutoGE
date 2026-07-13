#include "PlutoGE/render/passes/PostProcessPass.h"

#include "PlutoGE/render/postprocess/IPostProcessEffect.h"
#include "PlutoGE/render/postprocess/SSAOEffect.h"
#include "PlutoGE/render/postprocess/TAAEffect.h"
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
            return effect && (effect->GetTypeName() == "SSGI" || effect->GetTypeName() == "LPV" || effect->GetTypeName() == "RSM" || effect->GetTypeName() == "VCTGI");
        }

        bool IsPreParticleEffect(const IPostProcessEffect *effect)
        {
            return dynamic_cast<const SSAOEffect *>(effect) != nullptr;
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

        void BlitDepthBuffer(RenderTarget *source, RenderTarget *destination)
        {
            if (!source || !destination || source == destination)
            {
                return;
            }

            glBindFramebuffer(GL_READ_FRAMEBUFFER, source->GetFramebufferID());
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination->GetFramebufferID());
            glBlitFramebuffer(
                0, 0, source->GetWidth(), source->GetHeight(),
                0, 0, destination->GetWidth(), destination->GetHeight(),
                GL_DEPTH_BUFFER_BIT,
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

        std::vector<std::string> effectStates;
        effectStates.reserve(effects.size());
        for (const auto *effect : effects)
        {
            if (!effect)
            {
                effectStates.emplace_back("<null>");
                continue;
            }

            std::string state = effect->GetTypeName();
            state += effect->IsEnabled() ? "\nenabled" : "\ndisabled";
            for (const auto &parameter : effect->GetParameters())
            {
                state += '\n';
                state += parameter.name;
                state += '=';
                state += parameter.value;
            }
            effectStates.push_back(std::move(state));
        }

        // One pass instance renders multiple viewports. Keep their stack state
        // separate so rendering the game view cannot mask a change in the
        // editor view (or vice versa).
        const RenderTarget *viewportKey = ctx.renderTarget;
        auto previousStates = m_previousEffectStates.find(viewportKey);
        const bool chainStateChanged = previousStates != m_previousEffectStates.end() &&
                                       effectStates != previousStates->second;
        m_previousEffectStates[viewportKey] = std::move(effectStates);
        if (chainStateChanged)
        {
            for (auto *effect : effects)
            {
                if (auto *taa = dynamic_cast<TAAEffect *>(effect))
                {
                    taa->ResetHistory();
                }
            }
        }

        if (effects.empty())
        {
            BlitColorBuffer(ctx.temporaryRenderTarget, ctx.renderTarget);
            return;
        }

        size_t enabledEffectCount = 0;
        for (const auto &effect : effects)
        {
            if (effect && effect->IsEnabled() && !IsLightingManagedEffect(effect) && !IsPreParticleEffect(effect))
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

        // Every stage reads the same scene depth. Populate each reusable output
        // once instead of copying depth again for every effect in the chain.
        BlitDepthBuffer(ctx.temporaryRenderTarget, scratchB);
        BlitDepthBuffer(ctx.temporaryRenderTarget, ctx.renderTarget);

        for (size_t index = 0; index < effects.size(); ++index)
        {
            auto *effect = effects[index];
            if (!effect || !effect->IsEnabled() || IsLightingManagedEffect(effect) || IsPreParticleEffect(effect))
            {
                continue;
            }

            ++appliedEffectCount;
            const bool isLastEffect = appliedEffectCount == enabledEffectCount;
            RenderTarget *destination = isLastEffect ? ctx.renderTarget : nextIntermediate;
            if (!destination && !isLastEffect)
            {
                continue;
            }

            // Post-process targets are reused between frames. Seed every stage
            // with its current input so an effect that is disabled or returns
            // early can never expose stale output from a previous frame.
            BlitColorBuffer(source, destination);

            const bool gpuTimingActive = ctx.renderer && ctx.renderer->BeginPostProcessEffectTiming(effect->GetTypeName());
            effect->Apply(PostProcessContext{
                .renderContext = ctx,
                .sourceRenderTarget = source,
                .destinationRenderTarget = destination,
                .copyDepthToDestination = false,
            });
            if (gpuTimingActive)
            {
                ctx.renderer->EndPostProcessEffectTiming();
            }

            if (!isLastEffect)
            {
                source = destination;
                nextIntermediate = (destination == scratchA) ? scratchB : scratchA;
            }
        }
    }

}
