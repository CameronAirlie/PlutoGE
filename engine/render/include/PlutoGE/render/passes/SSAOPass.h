#pragma once

#include "PlutoGE/render/passes/IRenderPass.h"
#include "PlutoGE/render/postprocess/SSAOEffect.h"

namespace PlutoGE::render
{
    class SSAOPass : public IRenderPass
    {
    public:
        void Initialize() override;
        void Execute(const RenderContext &ctx) override;
        const char *GetName() const override { return "SSAO"; }

    private:
        SSAOEffect m_ssaoEffect;
    };
}