#pragma once

#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/postprocess/ShaderPostProcessEffect.h"

#include <memory>
#include <array>
#include <cstdint>

#include <glm/glm.hpp>

namespace PlutoGE::render
{
    class Shader;

    class VolumetricFogEffect : public ShaderPostProcessEffect
    {
    public:
        VolumetricFogEffect() = default;
        ~VolumetricFogEffect() override;

        void Initialize() override;
        void Apply(const PostProcessContext &context) override;
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
        };
        [[nodiscard]] Settings GetSettings() const noexcept
        {
            return {m_fogColor, m_density, m_heightFalloff, m_heightOffset, m_shadowDetailDistance,
                    m_scattering, m_anisotropy, m_ambientContribution,
                    m_directionalContribution, m_maxOpacity, m_stepCount,
                    m_shadowStepStride, m_halfResolution, m_horizonHaze, m_hazeWidth, m_hazeDistance};
        }

    private:
        void EnsureInternalTarget(int width, int height);

        Shader *m_shader = nullptr;
        Shader *m_ambientShader = nullptr;
        Shader *m_temporalShader = nullptr;
        Shader *m_compositeShader = nullptr;
        unsigned int m_shadowCompareSampler = 0;
        std::unique_ptr<RenderTarget> m_fogRenderTarget;
        std::unique_ptr<RenderTarget> m_ambientRenderTarget;
        std::array<std::unique_ptr<RenderTarget>, 2> m_historyTargets;
        glm::mat4 m_previousViewProjection{1.0f};
        int m_historyIndex = 0;
        bool m_hasHistory = false;
        std::uint64_t m_lastHistoryFrame = 0;
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
        int m_stepCount = 16;
        int m_shadowStepStride = 2;
        int m_internalWidth = 0;
        int m_internalHeight = 0;
        bool m_halfResolution = true;
    };
}
