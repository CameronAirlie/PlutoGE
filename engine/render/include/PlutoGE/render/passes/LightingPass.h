#pragma once

#include "PlutoGE/render/passes/IRenderPass.h"

namespace PlutoGE::render
{
    class LightPropagationVolumePass;
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
        Shader *m_lightingPassShader = nullptr;
        Shader *m_indirectCompositeShader = nullptr;
    };
}