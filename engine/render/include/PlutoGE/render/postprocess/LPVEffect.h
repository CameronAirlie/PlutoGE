#pragma once

#include "PlutoGE/render/postprocess/IPostProcessEffect.h"

#include <algorithm>
#include <string>
#include <vector>

namespace PlutoGE::render
{
    class LPVEffect : public IPostProcessEffect
    {
    public:
        LPVEffect() = default;
        ~LPVEffect() override = default;

        void Initialize() override {}
        void Apply(const PostProcessContext &context) override {}
        std::string GetTypeName() const override { return "LPV"; }
        std::string GetDisplayName() const override { return "Light Propagation Volume"; }
        std::vector<PostProcessParameter> GetParameters() const override
        {
            return {
                PostProcessParameter{
                    .name = "Grid Resolution",
                    .type = PostProcessParameterType::Int,
                    .value = std::to_string(m_gridResolution),
                },
                PostProcessParameter{
                    .name = "Horizontal Coverage",
                    .type = PostProcessParameterType::Float,
                    .value = std::to_string(m_minimumHorizontalCoverage),
                },
                PostProcessParameter{
                    .name = "Vertical Coverage",
                    .type = PostProcessParameterType::Float,
                    .value = std::to_string(m_minimumVerticalCoverage),
                },
                PostProcessParameter{
                    .name = "Forward Bias",
                    .type = PostProcessParameterType::Float,
                    .value = std::to_string(m_forwardBiasFactor),
                },
                PostProcessParameter{
                    .name = "Recenter Hysteresis",
                    .type = PostProcessParameterType::Float,
                    .value = std::to_string(m_recenterHysteresisFraction),
                },
            };
        }
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override
        {
            for (const auto &parameter : parameters)
            {
                if (parameter.name == "Grid Resolution")
                {
                    m_gridResolution = std::clamp(std::stoi(parameter.value), 8, 48);
                }
                else if (parameter.name == "Horizontal Coverage")
                {
                    m_minimumHorizontalCoverage = std::clamp(std::stof(parameter.value), 32.0f, 512.0f);
                }
                else if (parameter.name == "Vertical Coverage")
                {
                    m_minimumVerticalCoverage = std::clamp(std::stof(parameter.value), 16.0f, 256.0f);
                }
                else if (parameter.name == "Forward Bias")
                {
                    m_forwardBiasFactor = std::clamp(std::stof(parameter.value), 0.0f, 0.75f);
                }
                else if (parameter.name == "Recenter Hysteresis")
                {
                    m_recenterHysteresisFraction = std::clamp(std::stof(parameter.value), 0.05f, 0.45f);
                }
            }
        }

        int GetGridResolution() const { return m_gridResolution; }
        float GetMinimumHorizontalCoverage() const { return m_minimumHorizontalCoverage; }
        float GetMinimumVerticalCoverage() const { return m_minimumVerticalCoverage; }
        float GetForwardBiasFactor() const { return m_forwardBiasFactor; }
        float GetRecenterHysteresisFraction() const { return m_recenterHysteresisFraction; }

    private:
        int m_gridResolution = 16;
        float m_minimumHorizontalCoverage = 96.0f;
        float m_minimumVerticalCoverage = 40.0f;
        float m_forwardBiasFactor = 0.35f;
        float m_recenterHysteresisFraction = 0.33f;
    };
}