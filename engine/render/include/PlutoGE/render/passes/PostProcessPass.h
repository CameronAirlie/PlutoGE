#pragma once

#include "PlutoGE/render/passes/IRenderPass.h"

#include <unordered_map>
#include <string>
#include <vector>

namespace PlutoGE::render
{
    class RenderTarget;

    class PostProcessPass : public IRenderPass
    {
    public:
        void Initialize() override;
        void Execute(const RenderContext &ctx) override;
        const char *GetName() const override { return "Post Process"; }

    private:
        std::unordered_map<const RenderTarget *, std::vector<std::string>> m_previousEffectStates;
    };

}
