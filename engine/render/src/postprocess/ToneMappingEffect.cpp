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
        }
    }

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

            vec3 ToneMap(vec3 color, float exposure)
            {
                color *= max(exposure, 0.0);
                return color / (color + vec3(1.0));
            }

            void main()
            {
                vec3 color = texture(uSceneTexture, UV).rgb;
                FragColor = vec4(ToneMap(color, uExposure), 1.0);
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
        DrawFullscreenTriangle();

        EndApply();
    }
}