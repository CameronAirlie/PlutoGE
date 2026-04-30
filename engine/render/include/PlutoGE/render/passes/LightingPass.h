#pragma once

#include "PlutoGE/render/passes/IRenderPass.h"

namespace PlutoGE::render
{
    class Renderer;
    class LightingPass : public IRenderPass
    {
    public:
        LightingPass() = default;
        ~LightingPass() = default;

        void Execute(const RenderContext &ctx) override;
    };
}