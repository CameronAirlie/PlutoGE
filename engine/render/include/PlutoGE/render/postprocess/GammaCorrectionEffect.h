#pragma once

#include "PlutoGE/render/postprocess/IPostProcessEffect.h"

namespace PlutoGE::render
{
    class GammaCorrectionEffect : public IPostProcessEffect
    {
    public:
        explicit GammaCorrectionEffect(float gamma = 2.2f) : m_gamma(gamma) {}
        ~GammaCorrectionEffect() override = default;

        std::string GetTypeName() const override { return "GammaCorrection"; }
        std::string GetDisplayName() const override { return "Gamma Correction"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;

        float GetGamma() const { return m_gamma; }
        void SetGamma(float gamma) { m_gamma = gamma; }

    private:
        float m_gamma = 2.2f;
    };
}