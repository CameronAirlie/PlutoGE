#pragma once

#include "PlutoGE/render/postprocess/IPostProcessEffect.h"

namespace PlutoGE::render
{
    class Shader;

    class ShaderPostProcessEffect : public IPostProcessEffect
    {
    protected:
        void BeginApply(const PostProcessContext &context) const;
        void EndApply() const;
        void BindCommonInputs(Shader *shader, const PostProcessContext &context) const;
        void DrawFullscreenTriangle() const;
    };
}