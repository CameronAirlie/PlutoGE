#pragma once

#include "PlutoGE/render/passes/IRenderPass.h"

#include <glad/glad.h>

namespace PlutoGE::render
{
    class Shader;

    class RuntimeUIPass : public IRenderPass
    {
    public:
        void Initialize() override;
        void Execute(const RenderContext &ctx) override;
        const char *GetName() const override { return "Runtime UI"; }

    private:
        Shader *m_shader = nullptr;
        Shader *m_textShader = nullptr;
        GLuint m_vao = 0;
        GLuint m_instanceVbo = 0;
    };
}
