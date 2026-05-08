#pragma once

#include "PlutoGE/render/postprocess/ShaderPostProcessEffect.h"

namespace PlutoGE::render
{
    class Shader;

    enum class FXAAQualityPreset
    {
        X2 = 0,
        X4,
    };

    class FXAAEffect : public ShaderPostProcessEffect
    {
    public:
        explicit FXAAEffect(FXAAQualityPreset qualityPreset = FXAAQualityPreset::X2) : m_qualityPreset(qualityPreset) {}
        ~FXAAEffect() override = default;

        void Initialize() override;
        void Apply(const PostProcessContext &context) override;
        std::string GetTypeName() const override { return "FXAA"; }
        std::string GetDisplayName() const override { return "FXAA"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;

        FXAAQualityPreset GetQualityPreset() const { return m_qualityPreset; }
        void SetQualityPreset(FXAAQualityPreset qualityPreset) { m_qualityPreset = qualityPreset; }

    private:
        Shader *m_shader = nullptr;
        FXAAQualityPreset m_qualityPreset = FXAAQualityPreset::X2;
    };
}