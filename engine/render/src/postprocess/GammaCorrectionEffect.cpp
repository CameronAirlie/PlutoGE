#include "PlutoGE/render/postprocess/GammaCorrectionEffect.h"

#include "PlutoGE/render/Shader.h"

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

    void GammaCorrectionEffect::Initialize()
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
            uniform float uGamma;

            void main()
            {
                vec3 color = texture(uSceneTexture, UV).rgb;
                color = pow(max(color, vec3(0.0)), vec3(1.0 / max(uGamma, 0.001)));
                FragColor = vec4(color, 1.0);
            }
        )";

        m_shader = Shader::Create(source);
    }

    void GammaCorrectionEffect::Apply(const PostProcessContext &context)
    {
        if (!m_shader || !context.sourceRenderTarget)
        {
            return;
        }

        BeginApply(context);

        m_shader->Bind();
        BindCommonInputs(m_shader, context);
        m_shader->SetUniform("uGamma", m_gamma);
        DrawFullscreenTriangle();

        EndApply();
    }
}