#pragma once

#include "PlutoGE/render/passes/IRenderPass.h"

#include <glad/glad.h>

namespace PlutoGE::render
{
    class Shader;

    class GridPass : public IRenderPass
    {
    public:
        GridPass() = default;
        ~GridPass() = default;

        void Initialize() override;
        void Execute(const RenderContext &ctx) override;
        const char *GetName() const override { return "Grid"; }

    private:
        Shader *m_gridShader = nullptr;
        GLuint m_vao = 0;
    };
}