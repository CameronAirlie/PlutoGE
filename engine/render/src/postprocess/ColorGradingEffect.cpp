#include "PlutoGE/render/postprocess/ColorGradingEffect.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace PlutoGE::render
{
    std::vector<PostProcessParameter> ColorGradingEffect::GetParameters() const
    {
        return {
            PostProcessParameter{
                .name = "Preset",
                .type = PostProcessParameterType::Enum,
                .value = std::to_string(static_cast<int>(m_preset)),
                .enumOptions = {"Custom", "Neutral", "Filmic", "Cinematic", "Teal & Orange", "Vintage", "Bleach Bypass", "Noir", "Dreamy"},
            },
            PostProcessParameter{
                .name = "Brightness",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_brightness),
            },
            PostProcessParameter{
                .name = "Contrast",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_contrast),
            },
            PostProcessParameter{
                .name = "Saturation",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_saturation),
            },
            PostProcessParameter{
                .name = "Temperature",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_temperature),
            },
            {.name = "Tint", .type = PostProcessParameterType::Float, .value = std::to_string(m_tint)},
            {.name = "Vibrance", .type = PostProcessParameterType::Float, .value = std::to_string(m_vibrance)},
            {.name = "Lift", .type = PostProcessParameterType::Float, .value = std::to_string(m_lift)},
            {.name = "Gamma", .type = PostProcessParameterType::Float, .value = std::to_string(m_gamma)},
            {.name = "Gain", .type = PostProcessParameterType::Float, .value = std::to_string(m_gain)},
            {.name = "Fade", .type = PostProcessParameterType::Float, .value = std::to_string(m_fade)},
            {.name = "Vignette", .type = PostProcessParameterType::Float, .value = std::to_string(m_vignette)},
            {.name = "Film Grain", .type = PostProcessParameterType::Float, .value = std::to_string(m_grain)},
            {.name = "Shadow Color", .type = PostProcessParameterType::Color, .value = std::to_string(m_shadowColor.r) + "," + std::to_string(m_shadowColor.g) + "," + std::to_string(m_shadowColor.b) + ",1.0"},
            {.name = "Shadow Color Strength", .type = PostProcessParameterType::Float, .value = std::to_string(m_shadowColorStrength)},
            {.name = "Highlight Color", .type = PostProcessParameterType::Color, .value = std::to_string(m_highlightColor.r) + "," + std::to_string(m_highlightColor.g) + "," + std::to_string(m_highlightColor.b) + ",1.0"},
            {.name = "Highlight Color Strength", .type = PostProcessParameterType::Float, .value = std::to_string(m_highlightColorStrength)},
            {.name = "Split Balance", .type = PostProcessParameterType::Float, .value = std::to_string(m_splitBalance)},
        };
    }

    void ColorGradingEffect::ApplyPreset(Preset preset)
    {
        m_preset = preset;
        // brightness, contrast, saturation, temperature, tint, vibrance,
        // lift, gamma, gain, fade, vignette, grain
        const float values[][12] = {
            {0.00f, 1.00f, 1.00f,  0.00f,  0.00f, 0.00f, 0.00f, 1.00f, 1.00f, 0.00f, 0.00f, 0.00f},
            {0.00f, 1.00f, 1.00f,  0.00f,  0.00f, 0.00f, 0.00f, 1.00f, 1.00f, 0.00f, 0.00f, 0.00f},
            {-0.01f,1.12f, 0.96f,  0.06f,  0.02f, 0.08f,-0.01f, 0.96f, 1.04f, 0.03f, 0.20f, 0.035f},
            {-0.02f,1.18f, 0.92f, -0.05f,  0.03f, 0.12f,-0.02f, 0.94f, 1.06f, 0.02f, 0.32f, 0.025f},
            {-0.015f,1.22f,1.10f,  0.04f,  0.015f,0.18f,-0.018f,0.96f,1.05f,0.005f,0.24f,0.018f},
            {0.03f, 0.92f, 0.78f,  0.24f,  0.08f,-0.04f, 0.03f, 1.05f, 0.96f, 0.16f, 0.22f, 0.055f},
            {0.00f, 1.32f, 0.48f, -0.06f, -0.02f,-0.05f,-0.025f,0.91f,1.08f, 0.02f, 0.28f, 0.065f},
            {0.00f, 1.25f, 0.00f,  0.00f,  0.00f, 0.00f,-0.02f, 0.94f, 1.05f, 0.01f, 0.38f, 0.055f},
            {0.05f, 0.88f, 1.06f,  0.10f,  0.08f, 0.10f, 0.04f, 1.08f, 0.98f, 0.12f, 0.16f, 0.025f},
        };
        const auto &v = values[std::clamp(static_cast<int>(preset), 0, 8)];
        m_brightness=v[0]; m_contrast=v[1]; m_saturation=v[2]; m_temperature=v[3];
        m_tint=v[4]; m_vibrance=v[5]; m_lift=v[6]; m_gamma=v[7]; m_gain=v[8];
        m_fade=v[9]; m_vignette=v[10]; m_grain=v[11];
        m_shadowColorStrength = 0.0f;
        m_highlightColorStrength = 0.0f;
        m_splitBalance = 0.5f;
        if (preset == Preset::TealAndOrange)
        {
            m_shadowColor = {0.0f, 0.55f, 0.43f};
            m_highlightColor = {1.0f, 0.38f, 0.07f};
            m_shadowColorStrength = 0.62f;
            m_highlightColorStrength = 0.42f;
            m_splitBalance = 0.48f;
        }
    }

    void ColorGradingEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        auto preset = m_preset;
        for (const auto &parameter : parameters)
        {
            if (parameter.name == "Preset")
                preset = static_cast<Preset>(std::clamp(std::stoi(parameter.value), 0, 8));
        }
        if (preset != m_preset)
        {
            // Choosing Custom keeps the current look as a starting point.
            if (preset == Preset::Custom)
                m_preset = preset;
            else
                ApplyPreset(preset);
            return;
        }

        bool manuallyChanged = false;
        for (const auto &parameter : parameters)
        {
            auto setFloat = [&parameter, &manuallyChanged](float &target, float low, float high)
            {
                const float value = std::clamp(std::stof(parameter.value), low, high);
                manuallyChanged |= std::abs(value - target) > 0.00001f;
                target = value;
            };
            if (parameter.name == "Brightness")
            {
                setFloat(m_brightness, -1.0f, 1.0f);
            }
            else if (parameter.name == "Contrast")
            {
                setFloat(m_contrast, 0.0f, 3.0f);
            }
            else if (parameter.name == "Saturation")
            {
                setFloat(m_saturation, 0.0f, 3.0f);
            }
            else if (parameter.name == "Temperature")
            {
                setFloat(m_temperature, -1.0f, 1.0f);
            }
            else if (parameter.name == "Tint") setFloat(m_tint, -1.0f, 1.0f);
            else if (parameter.name == "Vibrance") setFloat(m_vibrance, -1.0f, 1.0f);
            else if (parameter.name == "Lift") setFloat(m_lift, -1.0f, 1.0f);
            else if (parameter.name == "Gamma") setFloat(m_gamma, 0.1f, 3.0f);
            else if (parameter.name == "Gain") setFloat(m_gain, 0.0f, 3.0f);
            else if (parameter.name == "Fade") setFloat(m_fade, 0.0f, 1.0f);
            else if (parameter.name == "Vignette") setFloat(m_vignette, 0.0f, 1.0f);
            else if (parameter.name == "Film Grain") setFloat(m_grain, 0.0f, 1.0f);
            else if (parameter.name == "Shadow Color" || parameter.name == "Highlight Color")
            {
                glm::vec3 color = parameter.name == "Shadow Color" ? m_shadowColor : m_highlightColor;
                float alpha = 1.0f;
                if (std::sscanf(parameter.value.c_str(), "%f,%f,%f,%f", &color.r, &color.g, &color.b, &alpha) >= 3)
                {
                    color = glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f));
                    glm::vec3 &target = parameter.name == "Shadow Color" ? m_shadowColor : m_highlightColor;
                    manuallyChanged |= glm::any(glm::greaterThan(glm::abs(color - target), glm::vec3(0.00001f)));
                    target = color;
                }
            }
            else if (parameter.name == "Shadow Color Strength") setFloat(m_shadowColorStrength, 0.0f, 1.0f);
            else if (parameter.name == "Highlight Color Strength") setFloat(m_highlightColorStrength, 0.0f, 1.0f);
            else if (parameter.name == "Split Balance") setFloat(m_splitBalance, 0.0f, 1.0f);
        }
        if (manuallyChanged)
            m_preset = Preset::Custom;
    }
}
