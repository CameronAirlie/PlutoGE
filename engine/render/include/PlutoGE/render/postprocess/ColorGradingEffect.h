#pragma once

#include "PlutoGE/render/postprocess/ShaderPostProcessEffect.h"

namespace PlutoGE::render
{
    class Shader;

    class ColorGradingEffect : public ShaderPostProcessEffect
    {
    public:
        ColorGradingEffect(float brightness = 0.0f, float contrast = 1.0f, float saturation = 1.0f, float temperature = 0.0f)
            : m_brightness(brightness), m_contrast(contrast), m_saturation(saturation), m_temperature(temperature)
        {
        }
        ~ColorGradingEffect() override = default;

        void Initialize() override;
        void Apply(const PostProcessContext &context) override;
        std::string GetTypeName() const override { return "ColorGrading"; }
        std::string GetDisplayName() const override { return "Color Grading"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;

        float GetBrightness() const { return m_brightness; }
        float GetContrast() const { return m_contrast; }
        float GetSaturation() const { return m_saturation; }
        float GetTemperature() const { return m_temperature; }

        void SetBrightness(float brightness) { m_brightness = brightness; }
        void SetContrast(float contrast) { m_contrast = contrast; }
        void SetSaturation(float saturation) { m_saturation = saturation; }
        void SetTemperature(float temperature) { m_temperature = temperature; }

    private:
        Shader *m_shader = nullptr;
        float m_brightness = 0.0f;
        float m_contrast = 1.0f;
        float m_saturation = 1.0f;
        float m_temperature = 0.0f;
    };
}