#pragma once

#include "PlutoGE/render/passes/IRenderPass.h"

namespace PlutoGE::render
{
    class Renderer;
    class Shader;
    class PostProcessPass : public IRenderPass
    {

    public:
        void Initialize() override;
        void Execute(const RenderContext &ctx) override;

    private:
        Shader *m_postProcessShader = nullptr;
    };

}