#include "PlutoGE/render/postprocess/SceneCompositeEffect.h"

#include "PlutoGE/render/Shader.h"

namespace PlutoGE::render
{
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