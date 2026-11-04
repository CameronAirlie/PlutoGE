#include "PlutoGE/render/postprocess/ToneMappingEffect.h"

#include <algorithm>

namespace PlutoGE::render
{
    std::vector<PostProcessParameter> ToneMappingEffect::GetParameters() const
    {
        return {
            PostProcessParameter{
                .name = "Exposure",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_exposure),
            },
            PostProcessParameter{
                .name = "Gamma",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_gamma),
            },
            PostProcessParameter{
                .name = "Curve",
                .type = PostProcessParameterType::Enum,
                .value = std::to_string(static_cast<int>(m_curve)),
                .enumOptions = {"ACES", "AgX", "PBR Neutral"},
            },
        };
    }

    void ToneMappingEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        for (const auto &parameter : parameters)
        {
            if (parameter.name == "Exposure")
            {
                m_exposure = std::stof(parameter.value);
            }
            else if (parameter.name == "Gamma")
            {
                m_gamma = (std::max)(std::stof(parameter.value), 0.001f);
            }
            else if (parameter.name == "Curve")
            {
                m_curve = static_cast<ToneMappingCurve>(std::clamp(std::stoi(parameter.value), 0,
                                                                   static_cast<int>(ToneMappingCurve::PbrNeutral)));
            }
        }
    }
}
