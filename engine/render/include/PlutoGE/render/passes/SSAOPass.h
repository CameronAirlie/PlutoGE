#pragma once

#include "PlutoGE/render/passes/IRenderPass.h"
#include "PlutoGE/render/postprocess/SSAOEffect.h"

#include <vector>

namespace PlutoGE::render
{
    class SSAOPass : public IRenderPass
    {
    public:
        void Initialize() override;
        void Execute(const RenderContext &ctx) override;
        const char *GetName() const override { return "SSAO"; }
        std::vector<PostProcessParameter> GetParameters() const { return m_ssaoEffect.GetParameters(); }
        void SetParameters(const std::vector<PostProcessParameter> &parameters) { m_ssaoEffect.SetParameters(parameters); }

    private:
        SSAOEffect m_ssaoEffect;
    };
}