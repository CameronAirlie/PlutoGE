#pragma once

#include "PlutoGE/render/postprocess/ShaderPostProcessEffect.h"

#include <chrono>

namespace PlutoGE::render
{
    class Shader;

    enum class DepthOfFieldQuality
    {
        Balanced = 0,
        High,
        Cinematic,
    };

    class DepthOfFieldEffect : public ShaderPostProcessEffect
    {
    public:
        DepthOfFieldEffect() = default;
        ~DepthOfFieldEffect() override = default;

        void Initialize() override;
        void Apply(const PostProcessContext &context) override;
        std::string GetTypeName() const override { return "DepthOfField"; }
        std::string GetDisplayName() const override { return "Depth of Field"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;

    private:
        float ReadAutoFocusDistance(const PostProcessContext &context);
        float LinearizeDepth(float depth, float nearPlane, float farPlane) const;
        void ResetFocus();

        Shader *m_shader = nullptr;
        DepthOfFieldQuality m_quality = DepthOfFieldQuality::High;
        float m_focusDistance = 8.0f;
        float m_currentFocusDistance = 8.0f;
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
        bool m_hasFocusDistance = false;
        std::chrono::steady_clock::time_point m_lastFocusUpdate;
    };
}
