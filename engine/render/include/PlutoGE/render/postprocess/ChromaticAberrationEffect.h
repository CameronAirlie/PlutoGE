#pragma once

#include "PlutoGE/render/postprocess/IPostProcessEffect.h"

namespace PlutoGE::render
{
    class ChromaticAberrationEffect : public IPostProcessEffect
    {
    public:
        explicit ChromaticAberrationEffect(float intensity = 0.003f) : m_intensity(intensity) {}

        std::string GetTypeName() const override { return "ChromaticAberration"; }
        std::string GetDisplayName() const override { return "Chromatic Aberration"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;

        [[nodiscard]] float GetIntensity() const noexcept { return m_intensity; }

    private:
        float m_intensity = 0.003f;
    };
}
