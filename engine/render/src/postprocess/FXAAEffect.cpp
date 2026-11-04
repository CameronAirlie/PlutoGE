#include "PlutoGE/render/postprocess/FXAAEffect.h"

#include <algorithm>

namespace PlutoGE::render
{
    std::vector<PostProcessParameter> FXAAEffect::GetParameters() const
    {
        return {
            PostProcessParameter{
                .name = "Quality",
                .type = PostProcessParameterType::Enum,
                .value = std::to_string(static_cast<int>(m_qualityPreset)),
                .enumOptions = {"2x", "4x"},
            },
        };
    }

    void FXAAEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        for (const auto &parameter : parameters)
        {
            if (parameter.name == "Quality")
            {
                const int qualityIndex = std::clamp(std::stoi(parameter.value), 0, 1);
                m_qualityPreset = static_cast<FXAAQualityPreset>(qualityIndex);
            }
        }
    }
}