#pragma once

#include "PlutoGE/render/postprocess/IPostProcessEffect.h"

namespace PlutoGE::render
{
    enum class FXAAQualityPreset
    {
        X2 = 0,
        X4,
    };

    class FXAAEffect : public IPostProcessEffect
    {
    public:
        explicit FXAAEffect(FXAAQualityPreset qualityPreset = FXAAQualityPreset::X2) : m_qualityPreset(qualityPreset) {}
        ~FXAAEffect() override = default;

        std::string GetTypeName() const override { return "FXAA"; }
        std::string GetDisplayName() const override { return "FXAA"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;

        FXAAQualityPreset GetQualityPreset() const { return m_qualityPreset; }
        void SetQualityPreset(FXAAQualityPreset qualityPreset) { m_qualityPreset = qualityPreset; }

    private:
        FXAAQualityPreset m_qualityPreset = FXAAQualityPreset::X2;
    };
}