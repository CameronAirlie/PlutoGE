#pragma once

#include "PlutoGE/render/passes/IRenderPass.h"

namespace PlutoGE::render
{
    class Shader;

    class ParticlePass : public IRenderPass
    {
    public:
        ParticlePass() = default;
        ~ParticlePass() override = default;

        void Initialize() override;
        void Execute(const RenderContext &ctx) override;
        const char *GetName() const override { return "Particles"; }

    private:
        Shader *m_updateShader = nullptr;
        Shader *m_renderShader = nullptr;
        bool m_loggedUnsupported = false;
    };
}
