#pragma once

#include "PlutoGE/render/postprocess/ShaderPostProcessEffect.h"

namespace PlutoGE::render
{
    class Shader;

    // The curve that maps scene radiance to display values (ColorOperations.slang toneMapCurve).
    enum class ToneMappingCurve : int
    {
        Aces,       // Narkowicz's ACES fit: contrasty; skews bright saturated hues
        AgX,        // filmic, hue-preserving; bright colours bleach towards white
        PbrNeutral, // Khronos PBR Neutral: base colours as authored, highlights compressed
    };

    class ToneMappingEffect : public ShaderPostProcessEffect
    {
    public:
        explicit ToneMappingEffect(float exposure = 1.0f, float gamma = 2.2f) : m_exposure(exposure), m_gamma(gamma) {}
        ~ToneMappingEffect() override = default;

        void Initialize() override;
        void Apply(const PostProcessContext &context) override;
        std::string GetTypeName() const override { return "ToneMapping"; }
        std::string GetDisplayName() const override { return "Tone Mapping"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;

        float GetExposure() const { return m_exposure; }
        void SetExposure(float exposure) { m_exposure = exposure; }

        float GetGamma() const { return m_gamma; }
        void SetGamma(float gamma) { m_gamma = gamma; }

        ToneMappingCurve GetCurve() const { return m_curve; }
        void SetCurve(ToneMappingCurve curve) { m_curve = curve; }

    private:
        Shader *m_shader = nullptr;
        float m_exposure = 1.0f;
        float m_gamma = 2.2f;
        // ACES keeps existing scenes as they were; AgX is the recommended look.
        ToneMappingCurve m_curve = ToneMappingCurve::Aces;
    };
}