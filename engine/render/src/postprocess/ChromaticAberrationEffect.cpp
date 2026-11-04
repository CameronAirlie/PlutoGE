#include "PlutoGE/render/postprocess/ChromaticAberrationEffect.h"

#include <algorithm>

namespace PlutoGE::render
{
    std::vector<PostProcessParameter> ChromaticAberrationEffect::GetParameters() const
    {
        return {{.name = "Intensity", .type = PostProcessParameterType::Float,
                 .value = std::to_string(m_intensity)}};
    }

    void ChromaticAberrationEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        for (const auto &parameter : parameters)
            if (parameter.name == "Intensity")
                m_intensity = std::clamp(std::stof(parameter.value), 0.0f, 0.1f);
    }
}
