#include "PlutoGE/render/postprocess/AutoExposureEffect.h"

#include <algorithm>

namespace PlutoGE::render
{
    std::vector<PostProcessParameter> AutoExposureEffect::GetParameters() const
    {
        return {
            PostProcessParameter{
                .name = "Key Value",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_keyValue),
            },
            PostProcessParameter{
                .name = "Min Exposure",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_minExposure),
            },
            PostProcessParameter{
                .name = "Max Exposure",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_maxExposure),
            },
            PostProcessParameter{
                .name = "Adapt Up",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_adaptationSpeedUp),
            },
            PostProcessParameter{
                .name = "Adapt Down",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_adaptationSpeedDown),
            },
        };
    }

    void AutoExposureEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        for (const auto &parameter : parameters)
        {
            if (parameter.name == "Key Value")
            {
                m_keyValue = std::max(std::stof(parameter.value), 0.001f);
            }
            else if (parameter.name == "Min Exposure")
            {
                m_minExposure = std::max(std::stof(parameter.value), 0.001f);
            }
            else if (parameter.name == "Max Exposure")
            {
                m_maxExposure = std::max(std::stof(parameter.value), m_minExposure);
            }
            else if (parameter.name == "Adapt Up")
            {
                m_adaptationSpeedUp = std::clamp(std::stof(parameter.value), 0.0f, 1.0f);
            }
            else if (parameter.name == "Adapt Down")
            {
                m_adaptationSpeedDown = std::clamp(std::stof(parameter.value), 0.0f, 1.0f);
            }
        }

        m_maxExposure = std::max(m_maxExposure, m_minExposure);
    }
}
