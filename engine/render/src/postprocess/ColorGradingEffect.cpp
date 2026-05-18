#include "PlutoGE/render/postprocess/ColorGradingEffect.h"

#include "PlutoGE/render/Shader.h"

namespace PlutoGE::render
{
    std::vector<PostProcessParameter> ColorGradingEffect::GetParameters() const
    {
        return {
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
        };
    }

    void ColorGradingEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        for (const auto &parameter : parameters)
        {
            if (parameter.name == "Brightness")
            {
                m_brightness = std::stof(parameter.value);
            }
            else if (parameter.name == "Contrast")
            {
                m_contrast = std::stof(parameter.value);
            }
            else if (parameter.name == "Saturation")
            {
                m_saturation = std::stof(parameter.value);
            }
            else if (parameter.name == "Temperature")
            {
                m_temperature = std::stof(parameter.value);
            }
        }
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

            void main()
            {
                vec4 source = texture(uSceneTexture, UV);
                vec3 color = source.rgb;

                color = max(color + vec3(uBrightness), vec3(0.0));
                color = ApplyContrast(color, uContrast);
                color = ApplySaturation(color, uSaturation);
                color = max(ApplyTemperature(color, uTemperature), vec3(0.0));

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
        DrawFullscreenTriangle();

        EndApply();
    }
}