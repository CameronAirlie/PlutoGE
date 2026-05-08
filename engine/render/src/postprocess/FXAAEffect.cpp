#include "PlutoGE/render/postprocess/FXAAEffect.h"

#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/Shader.h"

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

    void FXAAEffect::Initialize()
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
            uniform int uQualityPreset;

            float Luma(vec3 color)
            {
                return dot(color, vec3(0.299, 0.587, 0.114));
            }

            vec3 SampleScene(vec2 uv)
            {
                return texture(uSceneTexture, uv).rgb;
            }

            void main()
            {
                vec2 texelSize = 1.0 / vec2(textureSize(uSceneTexture, 0));

                vec3 colorCenter = SampleScene(UV);
                vec3 colorNorth = SampleScene(UV + vec2(0.0, texelSize.y));
                vec3 colorSouth = SampleScene(UV - vec2(0.0, texelSize.y));
                vec3 colorEast = SampleScene(UV + vec2(texelSize.x, 0.0));
                vec3 colorWest = SampleScene(UV - vec2(texelSize.x, 0.0));

                float lumaCenter = Luma(colorCenter);
                float lumaNorth = Luma(colorNorth);
                float lumaSouth = Luma(colorSouth);
                float lumaEast = Luma(colorEast);
                float lumaWest = Luma(colorWest);

                float lumaMin = min(lumaCenter, min(min(lumaNorth, lumaSouth), min(lumaEast, lumaWest)));
                float lumaMax = max(lumaCenter, max(max(lumaNorth, lumaSouth), max(lumaEast, lumaWest)));
                float contrast = lumaMax - lumaMin;

                if (contrast < max(0.0312, lumaMax * 0.125))
                {
                    FragColor = vec4(colorCenter, 1.0);
                    return;
                }

                vec2 edgeDirection;
                edgeDirection.x = -((lumaNorth + lumaSouth) - 2.0 * lumaCenter);
                edgeDirection.y = (lumaEast + lumaWest) - 2.0 * lumaCenter;

                float directionReduce = max((lumaNorth + lumaSouth + lumaEast + lumaWest) * 0.03125, 0.0078125);
                float reciprocalMinDirection = 1.0 / (min(abs(edgeDirection.x), abs(edgeDirection.y)) + directionReduce);
                edgeDirection = clamp(edgeDirection * reciprocalMinDirection, vec2(-8.0), vec2(8.0)) * texelSize;

                vec3 blendColor = 0.5 * (
                    SampleScene(UV + edgeDirection * (1.0 / 3.0 - 0.5)) +
                    SampleScene(UV + edgeDirection * (2.0 / 3.0 - 0.5))
                );

                if (uQualityPreset >= 1)
                {
                    blendColor = 0.25 * (
                        SampleScene(UV + edgeDirection * -0.5) +
                        SampleScene(UV + edgeDirection * -0.16666667) +
                        SampleScene(UV + edgeDirection * 0.16666667) +
                        SampleScene(UV + edgeDirection * 0.5)
                    );
                }

                FragColor = vec4(blendColor, 1.0);
            }
        )";

        m_shader = Shader::Create(source);
    }

    void FXAAEffect::Apply(const PostProcessContext &context)
    {
        if (!m_shader || !context.sourceRenderTarget)
        {
            return;
        }

        BeginApply(context);

        m_shader->Bind();
        BindCommonInputs(m_shader, context);
        m_shader->SetUniform("uQualityPreset", static_cast<int>(m_qualityPreset));
        DrawFullscreenTriangle();

        EndApply();
    }
}