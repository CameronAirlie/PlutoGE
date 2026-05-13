#pragma once

#include "PlutoGE/render/postprocess/IPostProcessEffect.h"

namespace PlutoGE::render
{
    class LPVEffect : public IPostProcessEffect
    {
    public:
        LPVEffect() = default;
        ~LPVEffect() override = default;

        void Initialize() override {}
        void Apply(const PostProcessContext &context) override {}
        std::string GetTypeName() const override { return "LPV"; }
        std::string GetDisplayName() const override { return "Light Propagation Volume"; }
    };
}