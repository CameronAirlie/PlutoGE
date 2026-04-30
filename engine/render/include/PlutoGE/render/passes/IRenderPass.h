#pragma once

namespace PlutoGE::render
{
    struct RenderContext;
    class IRenderPass
    {
    public:
        virtual ~IRenderPass() = default;

        virtual void Execute(const RenderContext &ctx) = 0;
    };
}