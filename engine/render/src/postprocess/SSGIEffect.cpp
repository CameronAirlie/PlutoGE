#include "PlutoGE/render/postprocess/SSGIEffect.h"

#include <algorithm>

namespace PlutoGE::render
{
    namespace
    {
        constexpr int kCompositeMode = 0;
        constexpr int kIndirectOnlyMode = 1;
        bool ParseBool(const std::string &value)
        {
            return value == "true" || value == "1";
        }
    }

    std::vector<std::string> SSGIEffect::GetSummary() const
    {
        return {
            "Rays that miss, or leave the screen, keep the ambient light already there (sky, probes or DDGI).",
            "Samples is rays per pixel (up to 4), Blur Radius the denoise passes (up to 3).",
        };
    }

    std::vector<PostProcessParameter> SSGIEffect::GetParameters() const
    {
        return {
            PostProcessParameter{
                .name = "Intensity",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_intensity),
            },
            PostProcessParameter{
                .name = "Ray Distance",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_rayDistance),
            },
            PostProcessParameter{
                .name = "Thickness",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_thickness),
            },
            PostProcessParameter{
                .name = "Samples",
                .type = PostProcessParameterType::Int,
                .value = std::to_string(m_sampleCount),
            },
            PostProcessParameter{
                .name = "Steps",
                .type = PostProcessParameterType::Int,
                .value = std::to_string(m_stepCount),
            },
            PostProcessParameter{
                .name = "Blur Radius",
                .type = PostProcessParameterType::Int,
                .value = std::to_string(m_blurRadius),
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
                .name = "Half Resolution",
                .type = PostProcessParameterType::Bool,
                .value = m_halfResolution ? "true" : "false",
            },
            PostProcessParameter{
                .name = "Indirect Only",
                .type = PostProcessParameterType::Bool,
                .value = m_outputMode == kIndirectOnlyMode ? "true" : "false",
            },
        };
    }

    void SSGIEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        for (const auto &parameter : parameters)
        {
            if (parameter.name == "Intensity")
            {
                m_intensity = std::clamp(std::stof(parameter.value), 0.0f, 4.0f);
            }
            else if (parameter.name == "Ray Distance")
            {
                m_rayDistance = std::clamp(std::stof(parameter.value), 0.25f, 12.0f);
            }
            else if (parameter.name == "Thickness")
            {
                m_thickness = std::clamp(std::stof(parameter.value), 0.01f, 2.0f);
            }
            else if (parameter.name == "Samples")
            {
                m_sampleCount = std::clamp(std::stoi(parameter.value), 1, 8);
            }
            else if (parameter.name == "Steps")
            {
                m_stepCount = std::clamp(std::stoi(parameter.value), 4, 32);
            }
            else if (parameter.name == "Blur Radius")
            {
                m_blurRadius = std::clamp(std::stoi(parameter.value), 0, kMaxBlurRadius);
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
            else if (parameter.name == "Half Resolution")
            {
                m_halfResolution = ParseBool(parameter.value);
            }
            else if (parameter.name == "Indirect Only")
            {
                m_outputMode = ParseBool(parameter.value) ? kIndirectOnlyMode : kCompositeMode;
            }
        }
    }
}
