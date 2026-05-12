#include "PlutoGE/render/postprocess/SceneCompositeEffect.h"

#include "PlutoGE/render/Shader.h"

#include <algorithm>

namespace PlutoGE::render
{
    namespace
    {
        bool ParseBool(const std::string &value)
        {
            return value == "true" || value == "1";
        }
    }

    std::vector<PostProcessParameter> SceneCompositeEffect::GetParameters() const
    {
        return {
            PostProcessParameter{
                .name = "Enable LPV",
                .type = PostProcessParameterType::Bool,
                .value = m_enableLpv ? "true" : "false",
            },
            PostProcessParameter{
                .name = "Enable SSGI",
                .type = PostProcessParameterType::Bool,
                .value = m_enableSsgi ? "true" : "false",
            },
            PostProcessParameter{
                .name = "Indirect Debug View",
                .type = PostProcessParameterType::Enum,
                .value = std::to_string(static_cast<int>(m_indirectDebugView)),
                .enumOptions = {"None", "LPV Only", "SSGI Only", "Combined Indirect"},
            },
        };
    }

    void SceneCompositeEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        for (const auto &parameter : parameters)
        {
            if (parameter.name == "Enable LPV")
            {
                m_enableLpv = ParseBool(parameter.value);
            }
            else if (parameter.name == "Enable SSGI")
            {
                m_enableSsgi = ParseBool(parameter.value);
            }
            else if (parameter.name == "Indirect Debug View")
            {
                const int debugView = std::clamp(std::stoi(parameter.value), 0, 3);
                m_indirectDebugView = static_cast<IndirectDebugView>(debugView);
            }
        }
    }

    void SceneCompositeEffect::Initialize()
    {
        m_shader = Shader::CreatePostProcessShader();
    }

    void SceneCompositeEffect::Apply(const PostProcessContext &context)
    {
        if (!m_shader || !context.sourceRenderTarget)
        {
            return;
        }

        BeginApply(context);

        m_shader->Bind();
        BindCommonInputs(m_shader, context);
        DrawFullscreenTriangle();

        EndApply();
    }
}