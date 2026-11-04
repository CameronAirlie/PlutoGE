#include "PlutoGE/render/postprocess/LensFlareEffect.h"

#include <algorithm>
#include <string>
#include <vector>

namespace PlutoGE::render
{
    namespace
    {
        float ReadFloatParameter(const PostProcessParameter &parameter, float fallback)
        {
            try
            {
                return std::stof(parameter.value);
            }
            catch (...)
            {
                return fallback;
            }
        }
    }

    std::vector<PostProcessParameter> LensFlareEffect::GetParameters() const
    {
        return {
            PostProcessParameter{
                .name = "Texture Path",
                .type = PostProcessParameterType::String,
                .value = m_texturePath,
            },
            PostProcessParameter{
                .name = "Intensity",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_intensity),
            },
            PostProcessParameter{
                .name = "Threshold",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_threshold),
            },
            PostProcessParameter{
                .name = "Scale",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_scale),
            },
            PostProcessParameter{
                .name = "Ghost Dispersal",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_ghostDispersal),
            },
        };
    }

    void LensFlareEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        for (const auto &parameter : parameters)
        {
            if (parameter.name == "Texture Path")
            {
                if (m_texturePath != parameter.value)
                {
                    m_texturePath = parameter.value;
                }
            }
            else if (parameter.name == "Intensity")
            {
                m_intensity = std::max(0.0f, ReadFloatParameter(parameter, m_intensity));
            }
            else if (parameter.name == "Threshold")
            {
                m_threshold = std::max(0.0f, ReadFloatParameter(parameter, m_threshold));
            }
            else if (parameter.name == "Scale")
            {
                m_scale = std::max(0.05f, ReadFloatParameter(parameter, m_scale));
            }
            else if (parameter.name == "Ghost Dispersal")
            {
                m_ghostDispersal = std::clamp(ReadFloatParameter(parameter, m_ghostDispersal), 0.0f, 2.0f);
            }
        }
    }
}
