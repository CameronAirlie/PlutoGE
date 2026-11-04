#include "PlutoGE/render/postprocess/VolumetricFogEffect.h"

#include <algorithm>

namespace PlutoGE::render
{
    namespace
    {
        constexpr int kMinStepCount = 16;
        constexpr int kMaxStepCount = 64;
        constexpr int kMinShadowStepStride = 1;
        constexpr int kMaxShadowStepStride = 4;

        bool ParseBool(const std::string &value)
        {
            return value == "true" || value == "1";
        }
    }

    std::vector<PostProcessParameter> VolumetricFogEffect::GetParameters() const
    {
        return {
            PostProcessParameter{
                .name = "Density",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_density),
            },
            PostProcessParameter{
                .name = "Height Falloff",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_heightFalloff),
            },
            PostProcessParameter{
                .name = "Height Offset",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_heightOffset),
            },
            PostProcessParameter{
                .name = "Shadow Detail Distance",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_shadowDetailDistance),
            },
            PostProcessParameter{
                .name = "Scattering",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_scattering),
            },
            PostProcessParameter{
                .name = "Anisotropy",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_anisotropy),
            },
            PostProcessParameter{
                .name = "Ambient Contribution",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_ambientContribution),
            },
            PostProcessParameter{
                .name = "Directional Contribution",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_directionalContribution),
            },
            PostProcessParameter{.name = "Horizon Haze", .type = PostProcessParameterType::Float, .value = std::to_string(m_horizonHaze)},
            PostProcessParameter{.name = "Haze Width", .type = PostProcessParameterType::Float, .value = std::to_string(m_hazeWidth)},
            PostProcessParameter{.name = "Haze Distance", .type = PostProcessParameterType::Float, .value = std::to_string(m_hazeDistance)},
            PostProcessParameter{.name = "Noise Strength", .type = PostProcessParameterType::Float, .value = std::to_string(m_noiseStrength)},
            PostProcessParameter{.name = "Noise Scale", .type = PostProcessParameterType::Float, .value = std::to_string(m_noiseScale)},
            PostProcessParameter{.name = "Noise Wind X", .type = PostProcessParameterType::Float, .value = std::to_string(m_noiseWind.x)},
            PostProcessParameter{.name = "Noise Wind Z", .type = PostProcessParameterType::Float, .value = std::to_string(m_noiseWind.y)},
            PostProcessParameter{
                .name = "Max Opacity",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_maxOpacity),
            },
            PostProcessParameter{
                .name = "Step Count",
                .type = PostProcessParameterType::Int,
                .value = std::to_string(m_stepCount),
            },
            PostProcessParameter{
                .name = "Shadow Step Stride",
                .type = PostProcessParameterType::Int,
                .value = std::to_string(m_shadowStepStride),
            },
            PostProcessParameter{
                .name = "Half Resolution",
                .type = PostProcessParameterType::Bool,
                .value = m_halfResolution ? "true" : "false",
            },
            PostProcessParameter{
                .name = "Color R",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_fogColor.r),
            },
            PostProcessParameter{
                .name = "Color G",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_fogColor.g),
            },
            PostProcessParameter{
                .name = "Color B",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_fogColor.b),
            },
        };
    }

    void VolumetricFogEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        for (const auto &parameter : parameters)
        {
            if (parameter.name == "Density")
            {
                m_density = std::max(std::stof(parameter.value), 0.0f);
            }
            else if (parameter.name == "Height Falloff")
            {
                m_heightFalloff = std::max(std::stof(parameter.value), 0.0f);
            }
            else if (parameter.name == "Height Offset")
            {
                m_heightOffset = std::stof(parameter.value);
            }
            else if ((parameter.name == "Shadow Detail Distance" || parameter.name == "Max Distance"))
            {
                m_shadowDetailDistance = std::max(std::stof(parameter.value), 0.1f);
            }
            else if (parameter.name == "Horizon Haze")
                m_horizonHaze = std::clamp(std::stof(parameter.value), 0.0f, 4.0f);
            else if (parameter.name == "Haze Width")
                m_hazeWidth = std::clamp(std::stof(parameter.value), 0.1f, 45.0f);
            else if (parameter.name == "Haze Distance")
                m_hazeDistance = std::clamp(std::stof(parameter.value), 1.0f, 1000000.0f);
            else if (parameter.name == "Noise Strength")
                m_noiseStrength = std::clamp(std::stof(parameter.value), 0.0f, 1.0f);
            else if (parameter.name == "Noise Scale")
                m_noiseScale = std::clamp(std::stof(parameter.value), 1.0f, 10000.0f);
            else if (parameter.name == "Noise Wind X")
                m_noiseWind.x = std::clamp(std::stof(parameter.value), -100.0f, 100.0f);
            else if (parameter.name == "Noise Wind Z")
                m_noiseWind.y = std::clamp(std::stof(parameter.value), -100.0f, 100.0f);
            else if (parameter.name == "Scattering")
            {
                m_scattering = glm::clamp(std::stof(parameter.value), 0.0f, 2.0f);
            }
            else if (parameter.name == "Anisotropy")
            {
                m_anisotropy = glm::clamp(std::stof(parameter.value), -0.85f, 0.85f);
            }
            else if (parameter.name == "Ambient Contribution")
            {
                m_ambientContribution = glm::clamp(std::stof(parameter.value), 0.0f, 4.0f);
            }
            else if (parameter.name == "Directional Contribution")
            {
                m_directionalContribution = glm::clamp(std::stof(parameter.value), 0.0f, 32.0f);
            }
            else if (parameter.name == "Max Opacity")
            {
                m_maxOpacity = glm::clamp(std::stof(parameter.value), 0.0f, 1.0f);
            }
            else if (parameter.name == "Step Count")
            {
                m_stepCount = std::clamp(std::stoi(parameter.value), kMinStepCount, kMaxStepCount);
            }
            else if (parameter.name == "Shadow Step Stride")
            {
                m_shadowStepStride = std::clamp(std::stoi(parameter.value), kMinShadowStepStride, kMaxShadowStepStride);
            }
            else if (parameter.name == "Half Resolution")
            {
                m_halfResolution = ParseBool(parameter.value);
            }
            else if (parameter.name == "Color R")
            {
                m_fogColor.r = glm::clamp(std::stof(parameter.value), 0.0f, 1.0f);
            }
            else if (parameter.name == "Color G")
            {
                m_fogColor.g = glm::clamp(std::stof(parameter.value), 0.0f, 1.0f);
            }
            else if (parameter.name == "Color B")
            {
                m_fogColor.b = glm::clamp(std::stof(parameter.value), 0.0f, 1.0f);
            }
        }
    }
}
