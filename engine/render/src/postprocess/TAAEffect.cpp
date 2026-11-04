#include "PlutoGE/render/postprocess/TAAEffect.h"

#include <algorithm>

namespace PlutoGE::render
{
    namespace
    {
        bool ParseBoolParameter(const std::string &value)
        {
            return value == "1" || value == "true" || value == "True";
        }
    }

    TAAEffect::TAAEffect(TAAEffectConfig config)
        : m_config(config)
    {
    }

    std::vector<PostProcessParameter> TAAEffect::GetParameters() const
    {
        return {
            PostProcessParameter{.name = "Quality", .type = PostProcessParameterType::Enum, .value = std::to_string(std::clamp(m_config.quality, 0, 1)), .enumOptions = {"Balanced", "High"}},
            PostProcessParameter{.name = "History Weight", .type = PostProcessParameterType::Float, .value = std::to_string(m_config.historyWeight)},
            PostProcessParameter{.name = "Stationary History Weight", .type = PostProcessParameterType::Float, .value = std::to_string(m_config.stationaryHistoryWeight)},
            PostProcessParameter{.name = "Motion History Weight", .type = PostProcessParameterType::Float, .value = std::to_string(m_config.motionHistoryWeight)},
            PostProcessParameter{.name = "Sharpening", .type = PostProcessParameterType::Float, .value = std::to_string(m_config.sharpening)},
            PostProcessParameter{.name = "Depth Rejection Threshold", .type = PostProcessParameterType::Float, .value = std::to_string(m_config.depthRejectionThreshold)},
            PostProcessParameter{.name = "Normal Rejection Threshold", .type = PostProcessParameterType::Float, .value = std::to_string(m_config.normalRejectionThreshold)},
            PostProcessParameter{.name = "Velocity Rejection Scale", .type = PostProcessParameterType::Float, .value = std::to_string(m_config.velocityRejectionScale)},
            PostProcessParameter{.name = "Jitter Strength", .type = PostProcessParameterType::Float, .value = std::to_string(m_config.jitterStrength)},
            PostProcessParameter{.name = "Jitter Enabled", .type = PostProcessParameterType::Bool, .value = m_config.jitterEnabled ? "true" : "false"},
            PostProcessParameter{.name = "Jitter Debug", .type = PostProcessParameterType::Bool, .value = m_config.jitterDebug ? "true" : "false"},
        };
    }

    void TAAEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        for (const auto &parameter : parameters)
        {
            if (parameter.name == "Quality")
            {
                m_config.quality = std::clamp(std::stoi(parameter.value), 0, 1);
            }
            else if (parameter.name == "History Weight")
            {
                m_config.historyWeight = std::clamp(std::stof(parameter.value), 0.0f, 0.99f);
            }
            else if (parameter.name == "Stationary History Weight")
            {
                m_config.stationaryHistoryWeight = std::clamp(std::stof(parameter.value), 0.0f, 0.99f);
            }
            else if (parameter.name == "Motion History Weight")
            {
                m_config.motionHistoryWeight = std::clamp(std::stof(parameter.value), 0.0f, 0.99f);
            }
            else if (parameter.name == "Sharpening")
            {
                m_config.sharpening = std::clamp(std::stof(parameter.value), 0.0f, 1.0f);
            }
            else if (parameter.name == "Depth Rejection Threshold")
            {
                m_config.depthRejectionThreshold = std::clamp(std::stof(parameter.value), 0.0f, 0.05f);
            }
            else if (parameter.name == "Normal Rejection Threshold")
            {
                m_config.normalRejectionThreshold = std::clamp(std::stof(parameter.value), 0.0f, 1.0f);
            }
            else if (parameter.name == "Velocity Rejection Scale")
            {
                m_config.velocityRejectionScale = std::clamp(std::stof(parameter.value), 0.0f, 400.0f);
            }
            else if (parameter.name == "Jitter Strength")
            {
                m_config.jitterStrength = std::clamp(std::stof(parameter.value), 0.0f, 2.0f);
            }
            else if (parameter.name == "Jitter Enabled")
            {
                m_config.jitterEnabled = ParseBoolParameter(parameter.value);
            }
            else if (parameter.name == "Jitter Debug")
            {
                m_config.jitterDebug = ParseBoolParameter(parameter.value);
            }
        }
    }
}
