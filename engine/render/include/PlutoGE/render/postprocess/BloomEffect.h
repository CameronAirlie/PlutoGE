#pragma once

#include "PlutoGE/render/postprocess/IPostProcessEffect.h"

namespace PlutoGE::render
{
    struct BloomSettings
    {
        float intensity = 0.75f;
        float threshold = 1.0f;
        float softKnee = 0.5f;
        float radius = 0.85f;
        int iterations = 6;
    };

    class BloomEffect : public IPostProcessEffect
    {
    public:
        std::string GetTypeName() const override { return "Bloom"; }
        std::string GetDisplayName() const override { return "Bloom"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;
        [[nodiscard]] BloomSettings GetSettings() const noexcept
        { return {m_intensity, m_threshold, m_softKnee, m_radius, m_iterations}; }

    private:
        float m_intensity = 0.75f;
        float m_threshold = 1.0f;
        float m_softKnee = 0.5f;
        float m_radius = 0.85f;
        int m_iterations = 6;
    };
}
