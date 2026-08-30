#include "PlutoGE/render/postprocess/ChromaticAberrationEffect.h"

#include "PlutoGE/render/Shader.h"

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

    void ChromaticAberrationEffect::Initialize()
    {
        ShaderSource source;
        source.vertexSource = R"(
            #version 330 core
            out vec2 UV;
            void main()
            {
                const vec2 vertices[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
                gl_Position = vec4(vertices[gl_VertexID], 0.0, 1.0);
                UV = gl_Position.xy * 0.5 + 0.5;
            })";
        source.fragmentSource = R"(
            #version 330 core
            in vec2 UV;
            out vec4 FragColor;
            uniform sampler2D uSceneTexture;
            uniform float uIntensity;
            void main()
            {
                vec2 radial = UV - 0.5;
                vec2 offset = radial * dot(radial, radial) * 2.0 * max(uIntensity, 0.0);
                FragColor = vec4(texture(uSceneTexture, clamp(UV + offset, 0.0, 1.0)).r,
                                 texture(uSceneTexture, UV).g,
                                 texture(uSceneTexture, clamp(UV - offset, 0.0, 1.0)).b, 1.0);
            })";
        m_shader = Shader::Create(source);
    }

    void ChromaticAberrationEffect::Apply(const PostProcessContext &context)
    {
        if (!m_shader || !context.sourceRenderTarget)
            return;
        BeginApply(context);
        m_shader->Bind();
        BindCommonInputs(m_shader, context);
        m_shader->SetUniform("uIntensity", m_intensity);
        DrawFullscreenTriangle();
        EndApply();
    }
}
