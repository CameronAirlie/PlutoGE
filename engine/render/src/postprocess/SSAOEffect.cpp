#include "PlutoGE/render/postprocess/SSAOEffect.h"

#include <algorithm>
#include <utility>

namespace PlutoGE::render
{
    namespace
    {
        constexpr int kCompositeMode = 0;
        constexpr int kAoOnlyMode = 1;
        constexpr int kMaxBlurRadius = 4;
        bool ParseBool(const std::string &value)
        {
            return value == "true" || value == "1";
        }
    }

    SSAOEffect::SSAOEffect() : SSAOEffect(SSAOEffectConfig{})
    {
    }

    SSAOEffect::SSAOEffect(SSAOEffectConfig config)
        : m_typeName(std::move(config.typeName)),
          m_displayName(std::move(config.displayName)),
          m_radius(config.radius),
          m_bias(config.bias),
          m_intensity(config.intensity),
          m_power(config.power),
          m_temporalBlend(config.temporalBlend),
          m_historyDepthThreshold(config.historyDepthThreshold),
          m_historyNormalThreshold(config.historyNormalThreshold),
          m_sampleCount(config.sampleCount),
          m_blurRadius(config.blurRadius),
          m_halfResolution(config.halfResolution),
          m_specularOcclusion(config.specularOcclusion)
    {
    }

    std::vector<PostProcessParameter> SSAOEffect::GetParameters() const
    {
        return {
            PostProcessParameter{
                .name = "Radius",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_radius),
            },
            PostProcessParameter{
                .name = "Bias",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_bias),
            },
            PostProcessParameter{
                .name = "Intensity",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_intensity),
            },
            PostProcessParameter{
                .name = "Power",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_power),
            },
            PostProcessParameter{
                .name = "Temporal Blend",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_temporalBlend),
            },
            PostProcessParameter{
                .name = "History Depth Threshold",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_historyDepthThreshold),
            },
            PostProcessParameter{
                .name = "History Normal Threshold",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_historyNormalThreshold),
            },
            PostProcessParameter{
                .name = "Samples",
                .type = PostProcessParameterType::Int,
                .value = std::to_string(m_sampleCount),
            },
            PostProcessParameter{
                .name = "Blur Radius",
                .type = PostProcessParameterType::Int,
                .value = std::to_string(m_blurRadius),
            },
            PostProcessParameter{
                .name = "Half Resolution",
                .type = PostProcessParameterType::Bool,
                .value = m_halfResolution ? "true" : "false",
            },
            PostProcessParameter{
                .name = "Specular Occlusion",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_specularOcclusion),
            },
            PostProcessParameter{
                .name = "AO Only",
                .type = PostProcessParameterType::Bool,
                .value = m_outputMode == kAoOnlyMode ? "true" : "false",
            },
        };
    }

    void SSAOEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        for (const auto &parameter : parameters)
        {
            if (parameter.name == "Radius")
            {
                m_radius = std::max(0.05f, std::stof(parameter.value));
            }
            else if (parameter.name == "Bias")
            {
                m_bias = std::clamp(std::stof(parameter.value), 0.0f, 0.5f);
            }
            else if (parameter.name == "Intensity")
            {
                m_intensity = std::clamp(std::stof(parameter.value), 0.0f, 4.0f);
            }
            else if (parameter.name == "Power")
            {
                m_power = std::clamp(std::stof(parameter.value), 0.25f, 4.0f);
            }
            else if (parameter.name == "Temporal Blend")
            {
                m_temporalBlend = std::clamp(std::stof(parameter.value), 0.0f, 0.98f);
            }
            else if (parameter.name == "History Depth Threshold")
            {
                m_historyDepthThreshold = std::clamp(std::stof(parameter.value), 0.001f, 0.25f);
            }
            else if (parameter.name == "History Normal Threshold")
            {
                m_historyNormalThreshold = std::clamp(std::stof(parameter.value), 0.0f, 0.999f);
            }
            else if (parameter.name == "Samples")
            {
                m_sampleCount = std::clamp(std::stoi(parameter.value), 4, kKernelSize);
            }
            else if (parameter.name == "Blur Radius")
            {
                m_blurRadius = std::clamp(std::stoi(parameter.value), 0, kMaxBlurRadius);
            }
            else if (parameter.name == "Half Resolution")
            {
                m_halfResolution = ParseBool(parameter.value);
            }
            else if (parameter.name == "Specular Occlusion")
            {
                m_specularOcclusion = std::clamp(std::stof(parameter.value), 0.0f, 1.0f);
            }
            else if (parameter.name == "AO Only")
            {
                m_outputMode = ParseBool(parameter.value) ? kAoOnlyMode : kCompositeMode;
            }
        }
    }
}
