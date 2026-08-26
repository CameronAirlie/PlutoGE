#pragma once

#include "PlutoGE/render/passes/IRenderPass.h"

#include <unordered_map>
#include <cstdint>
#include <vector>

namespace PlutoGE::render
{
    class IPostProcessEffect;
    class RenderTarget;

    class PostProcessPass : public IRenderPass
    {
    public:
        void Initialize() override;
        void Execute(const RenderContext &ctx) override;
        const char *GetName() const override { return "Post Process"; }

    private:
        struct EffectState
        {
            const IPostProcessEffect *effect = nullptr;
            std::uint64_t configurationRevision = 0;
            bool enabled = false;

            bool operator==(const EffectState &) const = default;
        };

        std::unordered_map<const RenderTarget *, std::vector<EffectState>> m_previousEffectStates;
    };

}
