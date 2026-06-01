#include "PlutoGE/render/postprocess/ToneMappingEffect.h"

#include "PlutoGE/render/Shader.h"

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
                m_gamma = std::stof(parameter.value);
            }
        }
    }

    // Add member initialization for gamma
    // ToneMappingEffect::ToneMappingEffect() : m_exposure(1.0f), m_gamma(2.2f) {}

    void ToneMappingEffect::Initialize()
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
            uniform float uExposure;
            uniform float uGamma;

            // Narkowicz 2015, "ACES Filmic Tone Mapping Curve"
            vec3 aces(vec3 x) {
                const float a = 2.51;
                const float b = 0.03;
                const float c = 2.43;
                const float d = 0.59;
                const float e = 0.14;
                return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
            }

            void main()
            {
                // 1. Fetch HDR color
                vec3 hdrColor = texture(uSceneTexture, UV).rgb;
                // 2. Apply exposure
                hdrColor *= max(uExposure, 0.0);
                // 3. Apply ACES tonemapping
                vec3 mapped = aces(hdrColor);
                // 4. Gamma correction for sRGB
                mapped = pow(mapped, vec3(1.0 / uGamma));
                FragColor = vec4(mapped, 1.0);
            }
        )";

        m_shader = Shader::Create(source);
    }

    void ToneMappingEffect::Apply(const PostProcessContext &context)
    {
        if (!m_shader || !context.sourceRenderTarget)
        {
            return;
        }

        BeginApply(context);

        m_shader->Bind();
        BindCommonInputs(m_shader, context);
        m_shader->SetUniform("uExposure", m_exposure);
        m_shader->SetUniform("uGamma", m_gamma);
        DrawFullscreenTriangle();

        EndApply();
    }
}