#include "PlutoGE/render/postprocess/ColorGradingEffect.h"

#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Shader.h"

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

    void ColorGradingEffect::Initialize()
    {
        ShaderSource source;

        source.vertexSource = R"(
            #version 330 core

            out vec2 UV;

            void main()
            {
                vec2 vertices[3] = vec2[3](
                    vec2(-1.0, -1.0),
                    vec2(3.0, -1.0),
                    vec2(-1.0, 3.0)
                );
                gl_Position = vec4(vertices[gl_VertexID], 0.0, 1.0);
                UV = 0.5 * gl_Position.xy + vec2(0.5);
            }
        )";

        source.fragmentSource = R"(
            #version 330 core

            in vec2 UV;
            out vec4 FragColor;

            uniform sampler2D uSceneTexture;
            uniform float uBrightness;
            uniform float uContrast;
            uniform float uSaturation;
            uniform float uTemperature;
            uniform float uTint;
            uniform float uVibrance;
            uniform float uLift;
            uniform float uGamma;
            uniform float uGain;
            uniform float uFade;
            uniform float uVignette;
            uniform float uGrain;
            uniform float uGrainSeed;
            uniform vec3 uShadowColor;
            uniform vec3 uHighlightColor;
            uniform float uShadowColorStrength;
            uniform float uHighlightColorStrength;
            uniform float uSplitBalance;

            vec3 ApplyContrast(vec3 color, float contrast)
            {
                return (color - vec3(0.5)) * max(contrast, 0.0) + vec3(0.5);
            }

            vec3 ApplySaturation(vec3 color, float saturation)
            {
                float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
                return mix(vec3(luma), color, max(saturation, 0.0));
            }

            vec3 ApplyTemperature(vec3 color, float temperature)
            {
                float t = clamp(temperature, -1.0, 1.0);
                vec3 balance = vec3(
                    1.0 + 0.1 * max(t, 0.0),
                    1.0 - 0.05 * abs(t),
                    1.0 + 0.1 * max(-t, 0.0));
                return color * balance;
            }

            vec3 ApplySplitToning(vec3 color)
            {
                float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
                float crossover = mix(0.25, 0.75, uSplitBalance);
                float shadows = 1.0 - smoothstep(crossover - 0.20, crossover + 0.12, luma);
                float highlights = smoothstep(crossover - 0.12, crossover + 0.24, luma);
                const vec3 luminanceWeights = vec3(0.2126, 0.7152, 0.0722);
                vec3 shadowTone = luma * uShadowColor /
                                  max(dot(uShadowColor, luminanceWeights), 0.001);
                vec3 highlightTone = luma * uHighlightColor /
                                     max(dot(uHighlightColor, luminanceWeights), 0.001);
                color = mix(color, shadowTone, shadows * uShadowColorStrength);
                color = mix(color, highlightTone, highlights * uHighlightColorStrength);
                return max(color, vec3(0.0));
            }

            uint Hash(uint value)
            {
                // Integer avalanche hash avoids the directional correlation
                // produced by translating a sine hash between frames.
                value ^= value >> 16u;
                value *= 0x7feb352du;
                value ^= value >> 15u;
                value *= 0x846ca68bu;
                value ^= value >> 16u;
                return value;
            }

            float GrainNoise(uvec2 pixel, uint frame)
            {
                uint value = pixel.x * 0x9e3779b9u;
                value ^= pixel.y * 0x85ebca6bu;
                value ^= frame * 0xc2b2ae35u;
                return float(Hash(value) & 0x00ffffffu) / 16777216.0;
            }

            void main()
            {
                vec4 source = texture(uSceneTexture, UV);
                vec3 color = source.rgb;

                color = max(color + vec3(uBrightness), vec3(0.0));
                color = ApplyContrast(color, uContrast);
                color = ApplySaturation(color, uSaturation);
                color = max(ApplyTemperature(color, uTemperature), vec3(0.0));
                color *= vec3(1.0 + 0.05 * uTint, 1.0 + 0.10 * uTint, 1.0 - 0.05 * uTint);

                color = ApplySplitToning(color);

                float peak = max(color.r, max(color.g, color.b));
                float average = (color.r + color.g + color.b) / 3.0;
                float colorfulness = peak - average;
                color = mix(color, color + (color - vec3(average)) * (1.0 - colorfulness), uVibrance);

                color = max(color + vec3(uLift), vec3(0.0));
                color = pow(color, vec3(1.0 / max(uGamma, 0.1))) * uGain;
                color = mix(color, vec3(dot(color, vec3(0.2126, 0.7152, 0.0722))), uFade * 0.35);
                color = mix(color, vec3(0.5), uFade * 0.18);

                vec2 centered = UV * 2.0 - 1.0;
                float vignette = smoothstep(0.35, 1.35, dot(centered, centered));
                color *= 1.0 - vignette * uVignette * 0.65;
                float grain = GrainNoise(uvec2(gl_FragCoord.xy), uint(uGrainSeed)) - 0.5;
                color = max(color + grain * uGrain * (0.5 + 0.5 * (1.0 - dot(color, vec3(0.2126, 0.7152, 0.0722)))), vec3(0.0));

                FragColor = vec4(color, source.a);
            }
        )";

        m_shader = Shader::Create(source);
    }

    void ColorGradingEffect::Apply(const PostProcessContext &context)
    {
        if (!m_shader || !context.sourceRenderTarget)
        {
            return;
        }

        BeginApply(context);

        m_shader->Bind();
        BindCommonInputs(m_shader, context);
        m_shader->SetUniform("uBrightness", m_brightness);
        m_shader->SetUniform("uContrast", m_contrast);
        m_shader->SetUniform("uSaturation", m_saturation);
        m_shader->SetUniform("uTemperature", m_temperature);
        m_shader->SetUniform("uTint", m_tint);
        m_shader->SetUniform("uVibrance", m_vibrance);
        m_shader->SetUniform("uLift", m_lift);
        m_shader->SetUniform("uGamma", m_gamma);
        m_shader->SetUniform("uGain", m_gain);
        m_shader->SetUniform("uFade", m_fade);
        m_shader->SetUniform("uVignette", m_vignette);
        m_shader->SetUniform("uGrain", m_grain);
        m_shader->SetUniform("uGrainSeed", static_cast<float>(context.renderContext.frameSequence % 4096u));
        m_shader->SetUniform("uShadowColor", m_shadowColor);
        m_shader->SetUniform("uHighlightColor", m_highlightColor);
        m_shader->SetUniform("uShadowColorStrength", m_shadowColorStrength);
        m_shader->SetUniform("uHighlightColorStrength", m_highlightColorStrength);
        m_shader->SetUniform("uSplitBalance", m_splitBalance);
        DrawFullscreenTriangle();

        EndApply();
    }
}
