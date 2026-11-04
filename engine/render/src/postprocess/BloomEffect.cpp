#include "PlutoGE/render/postprocess/BloomEffect.h"

#include <algorithm>
#include <string>

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

        int ReadIntParameter(const PostProcessParameter &parameter, int fallback)
        {
            try
            {
                return std::stoi(parameter.value);
            }
            catch (...)
            {
                return fallback;
            }
        }
    }

    std::vector<PostProcessParameter> BloomEffect::GetParameters() const
    {
        return {
            PostProcessParameter{.name = "Intensity", .type = PostProcessParameterType::Float, .value = std::to_string(m_intensity)},
            PostProcessParameter{.name = "Threshold", .type = PostProcessParameterType::Float, .value = std::to_string(m_threshold)},
            PostProcessParameter{.name = "Soft Knee", .type = PostProcessParameterType::Float, .value = std::to_string(m_softKnee)},
            PostProcessParameter{.name = "Radius", .type = PostProcessParameterType::Float, .value = std::to_string(m_radius)},
            PostProcessParameter{.name = "Iterations", .type = PostProcessParameterType::Int, .value = std::to_string(m_iterations)},
        };
    }

    void BloomEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        for (const auto &parameter : parameters)
        {
            if (parameter.name == "Intensity")
            {
                m_intensity = std::max(0.0f, ReadFloatParameter(parameter, m_intensity));
            }
            else if (parameter.name == "Threshold")
            {
                m_threshold = std::max(0.0f, ReadFloatParameter(parameter, m_threshold));
            }
            else if (parameter.name == "Soft Knee")
            {
                m_softKnee = std::clamp(ReadFloatParameter(parameter, m_softKnee), 0.0f, 1.0f);
            }
            else if (parameter.name == "Radius")
            {
                m_radius = std::clamp(ReadFloatParameter(parameter, m_radius), 0.25f, 2.5f);
            }
            else if (parameter.name == "Iterations")
            {
                m_iterations = std::clamp(ReadIntParameter(parameter, m_iterations), 1, 8);
            }
        }
    }
}
