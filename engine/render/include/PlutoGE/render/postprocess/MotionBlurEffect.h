#pragma once

#include "PlutoGE/render/postprocess/ShaderPostProcessEffect.h"

namespace PlutoGE::render
{
    class Shader;

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

    class MotionBlurEffect : public ShaderPostProcessEffect
    {
    public:
        MotionBlurEffect() = default;
        ~MotionBlurEffect() override = default;

        void Initialize() override;
        void Apply(const PostProcessContext &context) override;
        std::string GetTypeName() const override { return "MotionBlur"; }
        std::string GetDisplayName() const override { return "Motion Blur"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;
        [[nodiscard]] MotionBlurSettings GetSettings() const noexcept
        { return {m_quality, m_strength, m_shutterFraction, m_maxBlurRadius,
                  m_velocityThreshold, m_depthSeparationScale, m_centerWeight}; }

    private:
        Shader *m_shader = nullptr;
        MotionBlurQuality m_quality = MotionBlurQuality::Balanced;
        float m_strength = 1.0f;
        float m_shutterFraction = 0.5f;
        float m_maxBlurRadius = 20.0f;
        float m_velocityThreshold = 0.35f;
        float m_depthSeparationScale = 40.0f;
        float m_centerWeight = 1.0f;
    };
}
