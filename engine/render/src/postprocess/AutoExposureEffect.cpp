#include "PlutoGE/render/postprocess/AutoExposureEffect.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace PlutoGE::render
{
    namespace
    {
        // EV100 is log2(L * 100 / K) with the reflected-light calibration K = 12.5.
        constexpr float kEv100LuminanceScale = 100.0f / 12.5f;
        constexpr float kMinLuminance = 0.0001f; // Matches the shader's luminance floor.
        constexpr float kMaxLowLightDarkening = 4.0f;

        PostProcessParameter FloatParameter(const char *name, float value)
        {
            return {.name = name, .type = PostProcessParameterType::Float, .value = std::to_string(value)};
        }

        std::string Format(const char *format, float value)
        {
            char buffer[128];
            std::snprintf(buffer, sizeof(buffer), format, value);
            return buffer;
        }
    }

    AutoExposureEffect::AutoExposureEffect(Settings settings)
        : m_settings(settings)
    {
    }

    float AutoExposureEffect::LuminanceToEv100(float luminance) noexcept
    {
        return std::log2(std::max(luminance, kMinLuminance) * kEv100LuminanceScale);
    }

    float AutoExposureEffect::Ev100ToLuminance(float ev100) noexcept
    {
        return std::exp2(ev100) / kEv100LuminanceScale;
    }

    float AutoExposureEffect::ComputeTargetExposure(const Settings &settings, float averageLuminance) noexcept
    {
        const float luminance = std::max(averageLuminance, kMinLuminance);
        const float stopsBelowLowLight =
            std::max(0.0f, std::log2(Ev100ToLuminance(settings.lowLightEv) / luminance));
        const float exposure = settings.keyValue / luminance *
                               std::exp2(-stopsBelowLowLight * settings.lowLightDarkening);
        return std::clamp(exposure, std::min(settings.minExposure, settings.maxExposure),
                          std::max(settings.minExposure, settings.maxExposure));
    }

    std::vector<PostProcessParameter> AutoExposureEffect::GetParameters() const
    {
        return {
            FloatParameter("Key Value", m_settings.keyValue),
            FloatParameter("Min Exposure", m_settings.minExposure),
            FloatParameter("Max Exposure", m_settings.maxExposure),
            FloatParameter("Adapt Up", m_settings.adaptationSpeedUp),
            FloatParameter("Adapt Down", m_settings.adaptationSpeedDown),
            FloatParameter("Low Light EV", m_settings.lowLightEv),
            FloatParameter("Low Light Darkening", m_settings.lowLightDarkening),
        };
    }

    void AutoExposureEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        for (const auto &parameter : parameters)
        {
            if (parameter.name == "Key Value")
                m_settings.keyValue = std::max(std::stof(parameter.value), 0.001f);
            else if (parameter.name == "Min Exposure")
                m_settings.minExposure = std::max(std::stof(parameter.value), 0.001f);
            else if (parameter.name == "Max Exposure")
                m_settings.maxExposure = std::stof(parameter.value);
            else if (parameter.name == "Adapt Up")
                m_settings.adaptationSpeedUp = std::clamp(std::stof(parameter.value), 0.0f, 1.0f);
            else if (parameter.name == "Adapt Down")
                m_settings.adaptationSpeedDown = std::clamp(std::stof(parameter.value), 0.0f, 1.0f);
            else if (parameter.name == "Low Light EV")
                m_settings.lowLightEv = std::clamp(std::stof(parameter.value), -16.0f, 24.0f);
            else if (parameter.name == "Low Light Darkening")
                m_settings.lowLightDarkening =
                    std::clamp(std::stof(parameter.value), 0.0f, kMaxLowLightDarkening);
        }

        m_settings.maxExposure = std::max(m_settings.maxExposure, m_settings.minExposure);
    }

    std::vector<std::string> AutoExposureEffect::GetSummary() const
    {
        if (m_settings.lowLightDarkening <= 0.0f)
            return {"Every scene adapts toward Key Value, within Min/Max Exposure. "
                    "Raise Low Light Darkening to keep night and interiors darker."};
        return {
            Format("Scenes darker than EV %.1f lose ", m_settings.lowLightEv) +
                Format("%.2f stops of exposure per stop darker, ", m_settings.lowLightDarkening) +
                (m_settings.lowLightDarkening > 1.0f ? "so they are given less exposure as they darken."
                 : m_settings.lowLightDarkening == 1.0f ? "so exposure holds at the Low Light EV level."
                                                        : "so they brighten less than full adaptation."),
            "Min/Max Exposure still clamp the result.",
        };
    }
}
