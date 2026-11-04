#pragma once

#include "PlutoGE/render/postprocess/IPostProcessEffect.h"

namespace PlutoGE::render
{
    enum class MotionBlurQuality
    {
        Balanced = 0,
        High,
        Cinematic,
    };

    struct MotionBlurSettings
    {
        MotionBlurQuality quality;
        float strength, shutterFraction, maxBlurRadius, velocityThreshold, depthSeparationScale, centerWeight;
    };

    class MotionBlurEffect : public IPostProcessEffect
    {
    public:
        MotionBlurEffect() = default;
        ~MotionBlurEffect() override = default;

        std::string GetTypeName() const override { return "MotionBlur"; }
        std::string GetDisplayName() const override { return "Motion Blur"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;
        [[nodiscard]] MotionBlurSettings GetSettings() const noexcept
        { return {m_quality, m_strength, m_shutterFraction, m_maxBlurRadius,
                  m_velocityThreshold, m_depthSeparationScale, m_centerWeight}; }

    private:
        MotionBlurQuality m_quality = MotionBlurQuality::Balanced;
        float m_strength = 1.0f;
        float m_shutterFraction = 0.5f;
        float m_maxBlurRadius = 20.0f;
        float m_velocityThreshold = 0.35f;
        float m_depthSeparationScale = 40.0f;
        float m_centerWeight = 1.0f;
    };
}
