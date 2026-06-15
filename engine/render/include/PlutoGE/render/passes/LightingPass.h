#pragma once

#include "PlutoGE/render/passes/IRenderPass.h"

#include <memory>

namespace PlutoGE::scene
{
    struct Light;
}

namespace PlutoGE::render
{
    class LightPropagationVolumePass;
    class RenderTarget;
    class SceneCompositeEffect;
    class Shader;
    class LightingPass : public IRenderPass
    {
    public:
        LightingPass() = default;
        ~LightingPass() = default;

        void Initialize() override;
        void Execute(const RenderContext &ctx) override;
        const char *GetName() const override { return "Lighting"; }

    private:
        void EnsureShadowMaskTargets(int width, int height);
        RenderTarget *GenerateDirectionalShadowMask(const RenderContext &ctx, const scene::Light &light, bool filtered);

        Shader *m_lightingPassShader = nullptr;
        Shader *m_directLightingPassShader = nullptr;
        Shader *m_shadowMaskBlurShader = nullptr;
        Shader *m_indirectCompositeShader = nullptr;
        std::unique_ptr<RenderTarget> m_rawShadowMaskTarget;
        std::unique_ptr<RenderTarget> m_blurredShadowMaskTarget;
    };
}
