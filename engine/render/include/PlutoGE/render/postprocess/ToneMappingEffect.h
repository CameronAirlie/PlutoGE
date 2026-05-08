#pragma once

#include "PlutoGE/render/postprocess/ShaderPostProcessEffect.h"

namespace PlutoGE::render
{
    class Shader;

    class ToneMappingEffect : public ShaderPostProcessEffect
    {
    public:
        explicit ToneMappingEffect(float exposure = 1.0f) : m_exposure(exposure) {}
        ~ToneMappingEffect() override = default;

        void Initialize() override;
        void Apply(const PostProcessContext &context) override;
        std::string GetTypeName() const override { return "ToneMapping"; }
        std::string GetDisplayName() const override { return "Tone Mapping"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;

        float GetExposure() const { return m_exposure; }
        void SetExposure(float exposure) { m_exposure = exposure; }

    private:
        Shader *m_shader = nullptr;
        float m_exposure = 1.0f;
    };
}