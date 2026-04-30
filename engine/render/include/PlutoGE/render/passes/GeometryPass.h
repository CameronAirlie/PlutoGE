#pragma once

#include "PlutoGE/render/passes/IRenderPass.h"

namespace PlutoGE::render
{
    class Renderer;
    class GeometryPass : public IRenderPass
    {
    public:
        GeometryPass() = default;
        ~GeometryPass() = default;

        void Execute(const RenderContext &ctx) override;
    };
}