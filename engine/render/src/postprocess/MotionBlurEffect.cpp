#include "PlutoGE/render/postprocess/MotionBlurEffect.h"

#include <algorithm>

namespace PlutoGE::render
{
    std::vector<PostProcessParameter> MotionBlurEffect::GetParameters() const
    {
        return {
            PostProcessParameter{.name = "Quality", .type = PostProcessParameterType::Enum, .value = std::to_string(static_cast<int>(m_quality)), .enumOptions = {"Balanced", "High", "Cinematic"}},
            PostProcessParameter{.name = "Strength", .type = PostProcessParameterType::Float, .value = std::to_string(m_strength)},
            PostProcessParameter{.name = "Shutter Fraction", .type = PostProcessParameterType::Float, .value = std::to_string(m_shutterFraction)},
            PostProcessParameter{.name = "Max Blur Radius", .type = PostProcessParameterType::Float, .value = std::to_string(m_maxBlurRadius)},
            PostProcessParameter{.name = "Velocity Threshold", .type = PostProcessParameterType::Float, .value = std::to_string(m_velocityThreshold)},
            PostProcessParameter{.name = "Depth Separation Scale", .type = PostProcessParameterType::Float, .value = std::to_string(m_depthSeparationScale)},
            PostProcessParameter{.name = "Center Weight", .type = PostProcessParameterType::Float, .value = std::to_string(m_centerWeight)},
        };
    }

    void MotionBlurEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        for (const auto &parameter : parameters)
        {
            if (parameter.name == "Quality")
            {
                m_quality = static_cast<MotionBlurQuality>(std::clamp(std::stoi(parameter.value), 0, 2));
            }
            else if (parameter.name == "Strength")
            {
                m_strength = std::clamp(std::stof(parameter.value), 0.0f, 4.0f);
            }
            else if (parameter.name == "Shutter Fraction")
            {
                m_shutterFraction = std::clamp(std::stof(parameter.value), 0.0f, 1.5f);
            }
            else if (parameter.name == "Max Blur Radius")
            {
                m_maxBlurRadius = std::clamp(std::stof(parameter.value), 0.0f, 96.0f);
            }
            else if (parameter.name == "Velocity Threshold")
            {
                m_velocityThreshold = std::clamp(std::stof(parameter.value), 0.0f, 8.0f);
            }
            else if (parameter.name == "Depth Separation Scale")
            {
                m_depthSeparationScale = std::clamp(std::stof(parameter.value), 0.0f, 400.0f);
            }
            else if (parameter.name == "Center Weight")
            {
                m_centerWeight = std::clamp(std::stof(parameter.value), 0.0f, 8.0f);
            }
        }
    }
}
