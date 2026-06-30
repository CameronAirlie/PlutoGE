#pragma once

#include "PlutoGE/render/passes/IRenderPass.h"

#include <glad/glad.h>

namespace PlutoGE::render
{
    class Shader;

    class ParticlePass : public IRenderPass
    {
    public:
        ParticlePass() = default;
        ~ParticlePass() override;

        void Initialize() override;
        void Execute(const RenderContext &ctx) override;
        const char *GetName() const override { return "Particles"; }

    private:
        Shader *m_updateShader = nullptr;
        Shader *m_renderShader = nullptr;
        Shader *m_trailShader = nullptr;
        GLuint m_cpuParticleVao = 0;
        GLuint m_cpuParticleBuffer = 0;
        GLuint m_trailVao = 0;
        GLuint m_trailBuffer = 0;
        bool m_loggedUnsupported = false;
    };
}
