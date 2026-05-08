#pragma once

#include "PlutoGE/render/passes/IRenderPass.h"

namespace PlutoGE::render
{
    class PostProcessPass : public IRenderPass
    {
    public:
        void Initialize() override;
        void Execute(const RenderContext &ctx) override;
        const char *GetName() const override { return "Post Process"; }
    };

}