#pragma once

#include "PlutoGE/render/postprocess/IPostProcessEffect.h"

#include <glm/glm.hpp>

namespace PlutoGE::render
{
    class VolumetricFogEffect : public IPostProcessEffect
    {
    public:
        VolumetricFogEffect() = default;

        std::string GetTypeName() const override { return "VolumetricFog"; }
        std::string GetDisplayName() const override { return "Volumetric Fog"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;
        struct Settings
        {
            glm::vec3 color;
            float density, heightFalloff, heightOffset, shadowDetailDistance, scattering;
            float anisotropy, ambientContribution, directionalContribution, maxOpacity;
            int stepCount, shadowStepStride;
            bool halfResolution;
            float horizonHaze, hazeWidth, hazeDistance;
            float noiseStrength, noiseScale;
            glm::vec2 noiseWind; // World units per second along X and Z.
        };
        [[nodiscard]] Settings GetSettings() const noexcept
        {
            return {m_fogColor, m_density, m_heightFalloff, m_heightOffset, m_shadowDetailDistance,
                    m_scattering, m_anisotropy, m_ambientContribution,
                    m_directionalContribution, m_maxOpacity, m_stepCount,
                    m_shadowStepStride, m_halfResolution, m_horizonHaze, m_hazeWidth, m_hazeDistance,
                    m_noiseStrength, m_noiseScale, m_noiseWind};
        }

    private:
        glm::vec3 m_fogColor{1.0f, 1.0f, 1.0f};
        float m_density = 0.035f;
        float m_heightFalloff = 0.12f;
        float m_heightOffset = 0.0f;
        // Bounds shadow sampling only; height fog continues analytically to
        // the surface or infinity. SetParameters accepts legacy "Max Distance".
        float m_shadowDetailDistance = 80.0f;
        float m_scattering = 0.65f;
        float m_anisotropy = 0.2f;
        float m_ambientContribution = 1.0f;
        float m_directionalContribution = 6.0f;
        float m_maxOpacity = 1.0f;
        float m_horizonHaze = 1.0f;
        float m_hazeWidth = 4.0f;
        float m_hazeDistance = 2000.0f;
        // Mean-preserving density variation; Noise Scale is the feature size
        // in world units. RHI renderer only.
        float m_noiseStrength = 0.5f;
        float m_noiseScale = 40.0f;
        glm::vec2 m_noiseWind{1.5f, 0.5f};
        int m_stepCount = 16;
        int m_shadowStepStride = 2;
        bool m_halfResolution = true;
    };
}
