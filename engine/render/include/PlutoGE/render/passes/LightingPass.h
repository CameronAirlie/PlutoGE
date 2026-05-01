#pragma once

#include "PlutoGE/render/passes/IRenderPass.h"

namespace PlutoGE::render
{
    class Renderer;
    class Shader;
    class LightingPass : public IRenderPass
    {
    public:
        LightingPass() = default;
        ~LightingPass() = default;

        void Initialize() override;
        void Execute(const RenderContext &ctx) override;

    private:
        Shader *m_lightingPassShader = nullptr;
    };
}