#pragma once

#include "PlutoGE/render/passes/IRenderPass.h"

namespace PlutoGE::render
{
    class Renderer;
    class Shader;
    class ShadowPass : public IRenderPass
    {
    public:
        ShadowPass() = default;
        ~ShadowPass() = default;

        void Initialize() override;
        void Execute(const RenderContext &ctx) override;
        const char *GetName() const override { return "Shadow"; }

    private:
        Shader *m_shadowPassShader = nullptr;
        unsigned int m_shadowFramebuffer = 0;
    };
}