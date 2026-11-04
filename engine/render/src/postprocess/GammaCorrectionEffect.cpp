#include "PlutoGE/render/postprocess/GammaCorrectionEffect.h"

namespace PlutoGE::render
{
    std::vector<PostProcessParameter> GammaCorrectionEffect::GetParameters() const
    {
        return {
            PostProcessParameter{
                .name = "Gamma",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_gamma),
            },
        };
    }

    void GammaCorrectionEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        for (const auto &parameter : parameters)
        {
            if (parameter.name == "Gamma")
            {
                m_gamma = std::stof(parameter.value);
            }
        }
    }
}