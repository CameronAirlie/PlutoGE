#pragma once

#include "PlutoGE/render/postprocess/IPostProcessEffect.h"

namespace PlutoGE::render
{
    enum class DepthOfFieldQuality
    {
        Balanced = 0,
        High,
        Cinematic,
    };

    class DepthOfFieldEffect : public IPostProcessEffect
    {
    public:
        struct Settings
        {
            DepthOfFieldQuality quality = DepthOfFieldQuality::High;
            float focusDistance = 8.0f;
            float focalLength = 50.0f;
            float fStop = 2.8f;
            float sensorWidth = 36.0f;
            float maxBlurRadius = 5.0f;
            float nearBlurScale = 1.25f;
            float farBlurScale = 1.0f;
            float focusX = 0.5f;
            float focusY = 0.5f;
            float focusWindow = 0.08f;
            bool autoFocus = true;
        };

        DepthOfFieldEffect() = default;
        ~DepthOfFieldEffect() override = default;

        std::string GetTypeName() const override { return "DepthOfField"; }
        std::string GetDisplayName() const override { return "Depth of Field"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;
        [[nodiscard]] Settings GetSettings() const noexcept;

    private:
        DepthOfFieldQuality m_quality = DepthOfFieldQuality::High;
        float m_focusDistance = 8.0f;
        float m_focalLength = 50.0f;
        float m_fStop = 2.8f;
        float m_sensorWidth = 36.0f;
        float m_maxBlurRadius = 5.0f;
        float m_nearBlurScale = 1.25f;
        float m_farBlurScale = 1.0f;
        float m_focusSpeed = 5.0f;
        float m_focusX = 0.5f;
        float m_focusY = 0.5f;
        float m_focusWindow = 0.08f;
        bool m_autoFocus = true;
    };
}
