#pragma once

#include "PlutoGE/render/postprocess/IPostProcessEffect.h"

#include <string>

namespace PlutoGE::render
{
    struct SSAOEffectConfig
    {
        std::string typeName = "SSAO";
        std::string displayName = "Screen Space Ambient Occlusion";
        float radius = 0.85f;
        float bias = 0.08f;
        float intensity = 1.25f;
        float power = 1.5f;
        float temporalBlend = 0.9f;
        float historyDepthThreshold = 0.02f;
        float historyNormalThreshold = 0.85f;
        int sampleCount = 32;
        int blurRadius = 2;
        bool halfResolution = true;
        // How much ambient occlusion also occludes environment reflections (0 to 1), through the visibility of the
        // reflection lobe (SSAOUpsample.slang). SSR hits replace the occluded reflection where it finds them.
        float specularOcclusion = 1.0f;
    };

    class SSAOEffect : public IPostProcessEffect
    {
    public:
        SSAOEffect();
        explicit SSAOEffect(SSAOEffectConfig config);

        std::string GetTypeName() const override { return m_typeName; }
        std::string GetDisplayName() const override { return m_displayName; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;
        [[nodiscard]] SSAOEffectConfig GetConfig() const
        {
            return {m_typeName, m_displayName, m_radius, m_bias, m_intensity, m_power,
                    m_temporalBlend, m_historyDepthThreshold, m_historyNormalThreshold,
                    m_sampleCount, m_blurRadius, m_halfResolution, m_specularOcclusion};
        }
        [[nodiscard]] bool IsAoOnly() const noexcept { return m_outputMode != 0; }

    private:
        static constexpr int kKernelSize = 32;

        std::string m_typeName;
        std::string m_displayName;
        float m_radius = 0.0f;
        float m_bias = 0.0f;
        float m_intensity = 0.0f;
        float m_power = 0.0f;
        float m_temporalBlend = 0.0f;
        float m_historyDepthThreshold = 0.0f;
        float m_historyNormalThreshold = 0.0f;
        float m_specularOcclusion = 1.0f;
        int m_sampleCount = 0;
        int m_blurRadius = 0;
        int m_outputMode = 0;
        bool m_halfResolution = false;
    };
}
