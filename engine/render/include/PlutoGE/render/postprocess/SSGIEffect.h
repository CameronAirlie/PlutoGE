#pragma once

#include "PlutoGE/render/postprocess/IPostProcessEffect.h"

namespace PlutoGE::render
{
    class SSGIEffect : public IPostProcessEffect
    {
    public:
        SSGIEffect() = default;

        std::string GetTypeName() const override { return "SSGI"; }
        std::string GetDisplayName() const override { return "Screen Space Global Illumination"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;
        std::vector<std::string> GetSummary() const override;
        // The renderers trace min(sampleCount, 4) rays per pixel and use min(blurRadius, 3) edge-aware denoise passes.
        struct Settings
        {
            float intensity, rayDistance, thickness;
            int sampleCount, stepCount, blurRadius;
            bool halfResolution, indirectOnly;
            float temporalBlend, historyDepthThreshold, historyNormalThreshold;
        };
        [[nodiscard]] Settings GetSettings() const noexcept
        {
            return {m_intensity, m_rayDistance, m_thickness,
                    m_sampleCount, m_stepCount, m_blurRadius,
                    m_halfResolution, m_outputMode != 0, m_temporalBlend,
                    m_historyDepthThreshold, m_historyNormalThreshold};
        }

    private:
        static constexpr int kMaxBlurRadius = 4;

        float m_intensity = 1.0f;
        float m_rayDistance = 4.0f;
        float m_thickness = 0.45f;
        float m_temporalBlend = 0.88f;
        float m_historyDepthThreshold = 0.03f;
        float m_historyNormalThreshold = 0.9f;
        int m_sampleCount = 2;
        int m_stepCount = 16;
        int m_blurRadius = 2;
        int m_outputMode = 0;
        bool m_halfResolution = true;
    };
}
