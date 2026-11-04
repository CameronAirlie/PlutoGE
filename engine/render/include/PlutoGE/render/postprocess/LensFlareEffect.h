#pragma once

#include "PlutoGE/render/postprocess/IPostProcessEffect.h"

#include <string>

namespace PlutoGE::render
{
    struct LensFlareSettings { float intensity; float threshold; float scale; float ghostDispersal; };

    class LensFlareEffect : public IPostProcessEffect
    {
    public:
        std::string GetTypeName() const override { return "LensFlare"; }
        std::string GetDisplayName() const override { return "Lens Flare"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;
        [[nodiscard]] LensFlareSettings GetSettings() const noexcept
        { return {m_intensity, m_threshold, m_scale, m_ghostDispersal}; }

    private:
        std::string m_texturePath;
        float m_intensity = 0.35f;
        float m_threshold = 1.0f;
        float m_scale = 1.0f;
        float m_ghostDispersal = 0.55f;
    };
}
