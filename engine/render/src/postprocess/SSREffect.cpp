#include "PlutoGE/render/postprocess/SSREffect.h"

#include <algorithm>

namespace PlutoGE::render
{
    std::vector<PostProcessParameter> SSREffect::GetParameters() const
    {
        return {
            {.name = "Intensity", .type = PostProcessParameterType::Float, .value = std::to_string(m_intensity)},
            {.name = "Max Ray Distance", .type = PostProcessParameterType::Float, .value = std::to_string(m_maxRayDistance)},
            {.name = "Thickness", .type = PostProcessParameterType::Float, .value = std::to_string(m_thickness)},
            {.name = "Start Offset", .type = PostProcessParameterType::Float, .value = std::to_string(m_startOffset)},
            {.name = "Steps", .type = PostProcessParameterType::Int, .value = std::to_string(m_stepCount)},
            {.name = "Binary Search Steps", .type = PostProcessParameterType::Int, .value = std::to_string(m_binarySearchSteps)},
            {.name = "Rays per Pixel", .type = PostProcessParameterType::Int, .value = std::to_string(m_rayCount)},
            {.name = "Adaptive Rays", .type = PostProcessParameterType::Bool, .value = m_adaptiveRays ? "true" : "false"},
            {.name = "Edge Fade", .type = PostProcessParameterType::Float, .value = std::to_string(m_edgeFade)},
            {.name = "Max Roughness", .type = PostProcessParameterType::Float, .value = std::to_string(m_maxRoughness)},
            {.name = "Technique", .type = PostProcessParameterType::Enum, .value = m_reference ? "1" : "0",
             .enumOptions = {"Stochastic", "Reference"}},
        };
    }

    void SSREffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        for (const auto &parameter : parameters)
        {
            if (parameter.name == "Intensity") m_intensity = std::clamp(std::stof(parameter.value), 0.0f, 4.0f);
            else if (parameter.name == "Max Ray Distance") m_maxRayDistance = std::clamp(std::stof(parameter.value), 0.5f, 200.0f);
            else if (parameter.name == "Thickness") m_thickness = std::clamp(std::stof(parameter.value), 0.01f, 3.0f);
            else if (parameter.name == "Start Offset") m_startOffset = std::clamp(std::stof(parameter.value), 0.001f, 1.0f);
            else if (parameter.name == "Steps") m_stepCount = std::clamp(std::stoi(parameter.value), 8, 128);
            else if (parameter.name == "Binary Search Steps") m_binarySearchSteps = std::clamp(std::stoi(parameter.value), 0, 8);
            else if (parameter.name == "Rays per Pixel") m_rayCount = std::clamp(std::stoi(parameter.value), 1, 16);
            else if (parameter.name == "Adaptive Rays") m_adaptiveRays = parameter.value == "true";
            else if (parameter.name == "Edge Fade") m_edgeFade = std::clamp(std::stof(parameter.value), 0.001f, 0.5f);
            else if (parameter.name == "Max Roughness") m_maxRoughness = std::clamp(std::stof(parameter.value), 0.15f, 1.0f);
            else if (parameter.name == "Technique") m_reference = std::stoi(parameter.value) == 1;
            else if (parameter.name == "Fresnel Power") m_fresnelPower = std::clamp(std::stof(parameter.value), 0.25f, 10.0f);
            else if (parameter.name == "Metallic Boost") m_metallicBoost = std::clamp(std::stof(parameter.value), 0.0f, 1.0f);
        }
    }
}
