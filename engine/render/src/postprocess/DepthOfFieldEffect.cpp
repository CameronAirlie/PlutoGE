#include "PlutoGE/render/postprocess/DepthOfFieldEffect.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace PlutoGE::render
{
    namespace
    {
        bool ParseBoolParameter(const std::string &value)
        {
            return value == "1" || value == "true" || value == "True";
        }
    }

    std::vector<PostProcessParameter> DepthOfFieldEffect::GetParameters() const
    {
        return {
            PostProcessParameter{.name = "Quality", .type = PostProcessParameterType::Enum, .value = std::to_string(static_cast<int>(m_quality)), .enumOptions = {"Balanced", "High", "Cinematic"}},
            PostProcessParameter{.name = "Auto Focus", .type = PostProcessParameterType::Bool, .value = m_autoFocus ? "true" : "false"},
            PostProcessParameter{.name = "Focus Distance", .type = PostProcessParameterType::Float, .value = std::to_string(m_focusDistance)},
            PostProcessParameter{.name = "Focal Length (mm)", .type = PostProcessParameterType::Float, .value = std::to_string(m_focalLength)},
            PostProcessParameter{.name = "F-Stop", .type = PostProcessParameterType::Float, .value = std::to_string(m_fStop)},
            PostProcessParameter{.name = "Sensor Width (mm)", .type = PostProcessParameterType::Float, .value = std::to_string(m_sensorWidth)},
            PostProcessParameter{.name = "Max Blur Radius", .type = PostProcessParameterType::Float, .value = std::to_string(m_maxBlurRadius)},
            PostProcessParameter{.name = "Near Blur Scale", .type = PostProcessParameterType::Float, .value = std::to_string(m_nearBlurScale)},
            PostProcessParameter{.name = "Far Blur Scale", .type = PostProcessParameterType::Float, .value = std::to_string(m_farBlurScale)},
            PostProcessParameter{.name = "Focus Speed", .type = PostProcessParameterType::Float, .value = std::to_string(m_focusSpeed)},
            PostProcessParameter{.name = "Focus X", .type = PostProcessParameterType::Float, .value = std::to_string(m_focusX)},
            PostProcessParameter{.name = "Focus Y", .type = PostProcessParameterType::Float, .value = std::to_string(m_focusY)},
            PostProcessParameter{.name = "Focus Window", .type = PostProcessParameterType::Float, .value = std::to_string(m_focusWindow)},
        };
    }

    void DepthOfFieldEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        for (const auto &parameter : parameters)
        {
            if (parameter.name == "Quality")
            {
                m_quality = static_cast<DepthOfFieldQuality>(std::clamp(std::stoi(parameter.value), 0, 2));
            }
            else if (parameter.name == "Auto Focus")
            {
                m_autoFocus = ParseBoolParameter(parameter.value);
            }
            else if (parameter.name == "Focus Distance")
            {
                m_focusDistance = std::clamp(std::stof(parameter.value), 0.01f, 10000.0f);
            }
            else if (parameter.name == "Focal Length (mm)")
            {
                m_focalLength = std::clamp(std::stof(parameter.value), 8.0f, 300.0f);
            }
            else if (parameter.name == "F-Stop")
            {
                m_fStop = std::clamp(std::stof(parameter.value), 0.7f, 32.0f);
            }
            else if (parameter.name == "Sensor Width (mm)")
            {
                m_sensorWidth = std::clamp(std::stof(parameter.value), 4.0f, 70.0f);
            }
            else if (parameter.name == "Max Blur Radius")
            {
                m_maxBlurRadius = std::clamp(std::stof(parameter.value), 0.0f, 64.0f);
            }
            else if (parameter.name == "Near Blur Scale")
            {
                m_nearBlurScale = std::clamp(std::stof(parameter.value), 0.0f, 8.0f);
            }
            else if (parameter.name == "Far Blur Scale")
            {
                m_farBlurScale = std::clamp(std::stof(parameter.value), 0.0f, 8.0f);
            }
            else if (parameter.name == "Focus Speed")
            {
                const float speed = std::stof(parameter.value);
                // Presets made before physical DoF stored a per-frame blend in
                // [0, 1]. Convert it to an equivalent 60 Hz motor response.
                m_focusSpeed = speed <= 1.0f
                                   ? std::clamp(-std::log(std::max(1.0f - speed, 0.001f)) * 60.0f, 0.1f, 20.0f)
                                   : std::clamp(speed, 0.1f, 20.0f);
            }
            else if (parameter.name == "Focus X")
            {
                m_focusX = std::clamp(std::stof(parameter.value), 0.0f, 1.0f);
            }
            else if (parameter.name == "Focus Y")
            {
                m_focusY = std::clamp(std::stof(parameter.value), 0.0f, 1.0f);
            }
            else if (parameter.name == "Focus Window")
            {
                m_focusWindow = std::clamp(std::stof(parameter.value), 0.005f, 0.5f);
            }
        }
    }

    DepthOfFieldEffect::Settings DepthOfFieldEffect::GetSettings() const noexcept
    {
        return {m_quality, m_focusDistance, m_focalLength, m_fStop, m_sensorWidth,
                m_maxBlurRadius, m_nearBlurScale, m_farBlurScale,
                m_focusX, m_focusY, m_focusWindow, m_autoFocus};
    }
}
