#pragma once

#include "PlutoGE/render/postprocess/ShaderPostProcessEffect.h"

namespace PlutoGE::render
{
    class Shader;

    class ChromaticAberrationEffect : public ShaderPostProcessEffect
    {
    public:
        explicit ChromaticAberrationEffect(float intensity = 0.003f) : m_intensity(intensity) {}

        void Initialize() override;
        void Apply(const PostProcessContext &context) override;
        std::string GetTypeName() const override { return "ChromaticAberration"; }
        std::string GetDisplayName() const override { return "Chromatic Aberration"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;

        [[nodiscard]] float GetIntensity() const noexcept { return m_intensity; }

    private:
        Shader *m_shader = nullptr;
        float m_intensity = 0.003f;
    };
}
